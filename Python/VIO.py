import numpy as np
import cv2
import os
import gtsam            # libreria installata nel virtual environment (lo stesso di v2e)
import logging
import sys
import matplotlib.pyplot as plt

from scipy.spatial.transform import Rotation as R
from typing import Dict, List, Tuple
from gtsam.symbol_shorthand import X, V, B

from calibration_matrix import build_camera_intrinsics, build_T_imu_cam
from Event_reader import read_events_h5_array
from IMU_reader import read_imu_csv_array
from arc_star_detector import ArcStarDetector
from KLT_tracker import Feature, lk_track_batch, ensure_min_features
from time_surface import build_time_surface, should_build_TS, adaptive_tau
from ransac_utils import ransac_filter_flows
from window_utils import VIOWindow
from IMU_filter import imu_filter_pipeline
from IMU_utils import (_bias_from_state, _state_to_navstate, _navstate_to_state_dict,
                       _preintegrate_imu_between, _parse_bias_cfg, _make_state_dict, _build_preint_segments)
from plot_imu_timeseries import plot_imu_timeseries
from plot_utils import (plot_vio_map_3d, load_real_trajectory_csv, 
                        plot_trajectory_xy_with_ellipses, plot_trajectory_3d_with_tube)

# Comandi per creare il log
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('vio_pipeline.log', encoding='utf-8'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

# -------------------------------
# Helper functions
# -------------------------------

def log_factor_error_breakdown(graph: gtsam.NonlinearFactorGraph,
                               values: gtsam.Values,
                               logger: logging.Logger):
    """
    Funzione che scrive nel log gli errori della factor graph
    """
    stats = {}
    for i in range(int(graph.size())):
        try:
            fac = graph.at(i)
        except Exception:
            continue
        cls = type(fac).__name__
        try:
            err = float(fac.error(values))
        except Exception:
            err = float('nan')
        st = stats.get(cls, {'count': 0, 'sum': 0.0})
        st['count'] += 1
        if not np.isnan(err):
            st['sum'] += err
        stats[cls] = st

    logger.info("  Factor error breakdown:")
    for cls, st in stats.items():
        c = max(st['count'], 1)
        mean = st['sum'] / c
        logger.info(f"    {cls:>32}: count={st['count']:3d} sum={st['sum']:.6f} mean={mean:.6f}")

def stop_condition(N_TS: int, imu_meas: int) -> bool:
    """
    Condizione di chiusura della sliding window
    """
    if N_TS >= 15:
        return True
    if imu_meas >= 75:
        return True
    return False

def _state_from_isam_result(result: gtsam.Values,
                            k: int,
                            default_bias_acc: np.ndarray,
                            default_bias_gyro: np.ndarray) -> dict:
    """
    Build state dict (p,q,v,bias) from ISAM2 result for key k.
    Falls back if velocity/bias missing.
    """
    T_wi = result.atPose3(X(k))
    p = np.array(T_wi.translation(), dtype=np.float64)
    q = T_wi.rotation().toQuaternion()
    q_xyzw = np.array([q.x(), q.y(), q.z(), q.w()], dtype=np.float64)

    try:
        v = np.array(result.atVector(V(k)), dtype=np.float64).reshape(3)
    except Exception:
        v = np.zeros(3, dtype=np.float64)

    bias0_acc = default_bias_acc.copy()
    bias0_gyro = default_bias_gyro.copy()
    try:
        b = result.atConstantBias(B(k))
        bvec = np.array(b.vector(), dtype=np.float64).reshape(6)
        bias0_acc = bvec[0:3]
        bias0_gyro = bvec[3:6]
    except Exception:
        pass

    return _make_state_dict(p, q_xyzw, v, bias0_acc, bias0_gyro)

# -------------------------------
# Main VIO pipeline (ISAM2 + Post-hoc triangulation)
# -------------------------------

def run_vio(
    imu_csv_path: str,
    events_h5_path: str,
    translation_cam, rotation_cam, translation_imu, rotation_imu,
    imu_cfg: dict, cam_params: dict,
    output_dir: str = ".",
    max_window_keys: int = 14,
    overlap_keep: int = 2
):
    os.makedirs(output_dir, exist_ok=True)

    # Get IMU data
    imu_data = read_imu_csv_array(imu_csv_path)
    
    # Apply low-pass filter to IMU data
    acc_filt, gyro_filt = imu_filter_pipeline(imu_data[:, 1:4], imu_data[:, 4:7], 
                                              fs=imu_cfg.get('sample_rate_hz', 100.0), fc_acc=10.0, fc_gyro=10.0,
                                              order=4)
    
    # Plot IMU time series (raw vs filtered)
    plot_imu_timeseries(imu_data_raw=imu_data, acc_filt=acc_filt, gyro_filt=gyro_filt)

    # Reconstruct filtered IMU data
    imu_data[:, 1:4] = acc_filt
    imu_data[:, 4:7] = gyro_filt
        
    # Spike detection (for logging only) 
    ACC_SPIKE_THRESH = 4.75  # m/s^2 
    GYRO_SPIKE_THRESH = 0.8  # rad/s

    acc_mag = np.linalg.norm(imu_data[:, 1:4], axis=1)
    gyro_mag = np.linalg.norm(imu_data[:, 4:7], axis=1)

    spike_mask = (acc_mag > ACC_SPIKE_THRESH) | (gyro_mag > GYRO_SPIKE_THRESH)
    n_spikes = np.sum(spike_mask)

    # Non vengono modificati i dati, si controlla solo quante spike rimangono dopo il filtro
    if n_spikes > 0:
        logger.warning(f"Detected {n_spikes} IMU spikes ({100*n_spikes/len(imu_data):.2f}%)")
   
    # Get Events data
    events = read_events_h5_array(events_h5_path)

    # Allineamento dati IMU ed eventi: si taglia tutto ciò che è prima di t0_ref, 
    # e si shiftano i timestamp a partire da zero (t0_ref diventa t=0)
    # Questo viene fatto perché nella simulazione di UE c'è una fase statica 
    # per inizializzare il filtro (nel mio caso ho messo 5 secondi)

    # Dopo aver letto imu_data e events (prima di tagli)
    t0_ref = max(imu_data[0,0], 5.0)

    # Trim events from t0_ref and shift
    events = events[events[:,0] >= t0_ref]
    events[:,0] -= t0_ref

    # Trim IMU to same origin and same horizon
    imu_data = imu_data[imu_data[:,0] >= t0_ref]
    imu_data[:,0] -= t0_ref

    logger.info(f"Loaded {len(imu_data)} IMU samples and {len(events)} events")
    if imu_data.shape[0] == 0 or events.shape[0] == 0:
        logger.error("No IMU or events data available.")
        return [], {}, [], []

    t_start_global = min(imu_data[0, 0], events[0, 0])
    logger.info("Starting VIO with ISAM2...")

    # Intrinsics camera
    width = int(cam_params.get('width', 640))
    height = int(cam_params.get('height', 480))
    K = build_camera_intrinsics(cam_params)

    # Get IMU parameters
    g_scalar = float(imu_cfg.get('g', 3.72))
    noise_cfg = imu_cfg.get('noise', {'accel': 0.02, 'gyro': 0.001})
    bias_rw_cfg = imu_cfg.get('BiasRW', {'accel': 0.0002, 'gyro': 0.00003})
    bias_cfg = imu_cfg.get('bias', {'accel': [0.05, -0.02, 0.01], 'gyro': [0.002, -0.001, 0.0005]})
    b_a0, b_g0 = _parse_bias_cfg(bias_cfg)

    # IMU parameters for GTSAM
    acc_cov = float(noise_cfg.get('accel')*100) ** 2
    gyro_cov = float(noise_cfg.get('gyro')*100) ** 2

    imu_params_shared = gtsam.PreintegrationParams.MakeSharedU(g_scalar)
    imu_params_shared.setAccelerometerCovariance(np.eye(3) * acc_cov)
    imu_params_shared.setGyroscopeCovariance(np.eye(3) * gyro_cov)
    imu_params_shared.setIntegrationCovariance(np.eye(3) * 1e-8)

    imu_noise_for_window = {
        'accel_bias': float(bias_rw_cfg.get('accel', 0.0002)),
        'gyro_bias': float(bias_rw_cfg.get('gyro', 0.00003)),
    }

    # Extrinsics    
    T_cam_imu_pose3 = build_T_imu_cam(
        translation_cam=translation_cam,
        rotation_cam=rotation_cam,
        translation_imu=translation_imu,
        rotation_imu=rotation_imu,
        cam_frame="rdf"
    )

    # Detector parameters
    FILTER_THRESHOLD = 0.05
    det = ArcStarDetector(sensor_width=width, sensor_height=height, filter_threshold=FILTER_THRESHOLD)
    last_corner_time_map = np.zeros((height, width), dtype=np.float64)
    MIN_TIME_BETWEEN_CORNERS = 0.003
    MIN_DIST_BETWEEN_FEATURES = 8
    MAX_FEATURES_TOTAL = 5000
    MIN_FEATURES_TARGET = 500

    # TS parameters
    TAU_BASE = 0.0225
    EVENT_RATE_REF = 2e6
    DELTA_T_MAX = 0.050
    EVENT_THRESHOLD = 750000

    # Tracker
    PYR_MAX_LEVEL = 2
    LK_WIN_SIZE = (21, 21)
    LK_MAX_ITER = 60
    LK_EPS = 1e-3
    
    # RANSAC
    RANSAC_MODEL = "similarity"
    RANSAC_THRESH_PX = 3.0
    RANSAC_MAX_ITERS = 800
    RANSAC_CONFIDENCE = 0.995

    # ISAM2 setup
    isam_params = gtsam.ISAM2Params()
    isam_params.relinearizeSkip = 1
    isam_params.setRelinearizeThreshold(0.01)
    isam = gtsam.ISAM2(isam_params)
    prev_result: gtsam.Values | None = None

    # Results storage   
    optimized_pose_all: List[Tuple[float, np.ndarray, np.ndarray, np.ndarray]] = []
    optimized_features_all: Dict[int, dict] = {}
    optimized_depths_all: List[Tuple[int, float]] = []
    cov_all = []
    graph_total_error_history = []

    # ========== INITIAL STATE ==========
    
    # LINEAR Trajectory (initial conditions --> da impostare in base alla propria traiettoria)
    v = 0.25
    waypoints = [
        np.array([-177.3, -56.8, 0.0]),
        np.array([-171.4, -34.3, 0.0])
    ]
    delta = waypoints[1] - waypoints[0]
    alpha = np.arctan2(delta[1], delta[0])
    v0 = np.array([v * np.cos(alpha), v * np.sin(alpha), 0.0], dtype=np.float64)
    p0 = np.array([0.000000,0.000000,-0.001029], dtype=np.float64)
    q0_xyzw = np.array([-0.001620945,0.002979085,-0.127877821,0.991784130], dtype=np.float64)
    bias0_acc = b_a0.copy()
    bias0_gyro = b_g0.copy()

    # Initial covariance for first window (tre casistiche per lo studio di come variano i risultati)
    cov_0 = {
        'pose': np.diag([np.deg2rad(7.5)]*3 + [0.5, 0.5, 0.25]) ** 2,
        'vel': np.diag([0.3, 0.3, 0.15]) ** 2,
        'bias': np.diag([6e-3, 6e-3, 6e-3, 1e-5, 1e-5, 1e-5]) ** 2
    }      
    """
    cov_0 = {
        'pose': np.diag([np.deg2rad(20)]*3 + [1.5, 1.5, 0.8]) ** 2,
        'vel': np.diag([1.0, 1.0, 0.5]) ** 2,
        'bias': np.diag([6e-3, 6e-3, 6e-3, 1e-5, 1e-5, 1e-5]) ** 2
    }  
    """
    """
    cov_0 = {
        'pose': np.diag([np.deg2rad(2)]*3 + [0.15, 0.15, 0.10]) ** 2,
        'vel': np.diag([0.1, 0.1, 0.05]) ** 2,
        'bias': np.diag([6e-3, 6e-3, 6e-3, 1e-5, 1e-5, 1e-5]) ** 2
    }  
    """

    logger.info("Starting VIO loop with sliding window + ISAM2...")

    # VIO loop variables
    global_k: int = 0
    active_k_list: List[int] = []

    features: List[Feature] = []
    next_feature_id = 0
    prev_ts_frame = None

    t_curr = float(t_start_global)
    idx_events = 0
    idx_imu = 0
    t_prev_TS = float(t_start_global)
    events_since_last_TS = 0

    window_count = 0

    # Initialize window
    window = VIOWindow(T_cam_imu_pose3=T_cam_imu_pose3, K=K, meas_sigma_px=2.0)

    is_first_window = True
    init_state0 = _make_state_dict(p0, q0_xyzw, v0, bias0_acc, bias0_gyro)
    window.start(k0_idx=global_k, t_start=t_curr, init_state=init_state0)
    active_k_list.append(global_k)
    global_k += 1

    # Track last key time for preintegration
    last_key_time = float(t_curr)

    # MAIN LOOP: nella parte di simulation della tesi è spiegato in dettaglio come funziona

    while idx_events < len(events) or idx_imu < len(imu_data):

        # continua a leggere eventi e IMU fino a quando non raggiungi la fine di uno dei due, e aggiorna t_curr di conseguenza

        corner = 0
        N_TS = 0
        imu_meas = 0
        window_count += 1
        logger.info(f"\n{'='*60}\nStarting Window #{window_count} at t={t_curr:.3f}s")

        while True:
            while idx_imu < len(imu_data) and imu_data[idx_imu, 0] <= t_curr:
                imu_meas += 1
                idx_imu += 1

            # segna l'aggiunta di dati IMU al log, anche se non vengono usati subito

            t_ev = []
            x_ev = []
            y_ev = []
            p_ev = []
            while idx_events < len(events) and events[idx_events, 0] <= t_curr:
                t_ev.append(events[idx_events, 0])
                x_ev.append(events[idx_events, 1])
                y_ev.append(events[idx_events, 2])
                p_ev.append(events[idx_events, 3])
                idx_events += 1

            # cornert detection + tracking + RANSAC + TS building + preintegration + graph update (quando serve)

            for (t, x, y, p_bool) in zip(t_ev, x_ev, y_ev, p_ev):
                x_int = int(x)
                y_int = int(y)
                if not (0 <= x_int < width and 0 <= y_int < height):
                    continue

                # corner detection: se è corner e non è troppo vicino a un corner già usato recentemente, lo aggiungo alle features
                if det.is_corner(t, x_int, y_int, bool(p_bool)):
                    corner += 1
                    if t - last_corner_time_map[y_int, x_int] >= MIN_TIME_BETWEEN_CORNERS:
                        last_corner_time_map[y_int, x_int] = t
                        if len(features) < MIN_FEATURES_TARGET and len(features) < MAX_FEATURES_TOTAL:
                            too_close = any(
                                (abs(f.x - x_int) <= MIN_DIST_BETWEEN_FEATURES and abs(f.y - y_int) <= MIN_DIST_BETWEEN_FEATURES)
                                for f in features
                            )
                            if not too_close:
                                features.append(Feature(next_feature_id, float(x_int), float(y_int), t, interval_idx=0))
                                next_feature_id += 1

                events_since_last_TS += 1

                # costruisci la time surface e fai tracking solo se hai abbastanza eventi da quando hai costruito l'ultima TS, 
                # altrimenti aspetta

                if should_build_TS(events_since_last_TS, t_prev_TS, t, DELTA_T_MAX, EVENT_THRESHOLD):
                    delta_t_TS = t - t_prev_TS
                    tau_now = adaptive_tau(TAU_BASE, events_since_last_TS, delta_t_TS, EVENT_RATE_REF)
                    TS = build_time_surface(det.sae[1], det.sae[0], t_now=t, tau=tau_now)
                    TS_norm = (np.clip((TS + 1.0) * 0.5, 0.0, 1.0) * 255.0).astype(np.uint8)

                    # --- tracking KLT ---
                    if prev_ts_frame is not None and len(features) > 0:
                        old_pts = np.array([[f.x, f.y] for f in features], dtype=np.float32).reshape(-1, 1, 2)
                        new_pts, status, err = lk_track_batch(
                            prev_ts_frame, TS_norm, old_pts,
                            win_size=LK_WIN_SIZE,
                            max_level=PYR_MAX_LEVEL,
                            criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, LK_MAX_ITER, LK_EPS)
                        )
                        if new_pts is None:
                            new_pts = old_pts.copy()
                            status = np.zeros((old_pts.shape[0], 1), dtype=np.uint8)
                        new_pts = new_pts.reshape(-1, 2).astype(np.float32)
                        status = status.reshape(-1)

                        # --- RANSAC filter ---
                        lk_ok_idx = np.where(status == 1)[0]
                        if lk_ok_idx.size >= 2:
                            p_old = old_pts.reshape(-1, 2)[lk_ok_idx].astype(np.float32)
                            p_new = new_pts[lk_ok_idx].astype(np.float32)
                            inliers_mask, _M = ransac_filter_flows(
                                p_old, p_new,
                                model=RANSAC_MODEL,
                                ransac_thresh=RANSAC_THRESH_PX,
                                max_iters=RANSAC_MAX_ITERS,
                                confidence=RANSAC_CONFIDENCE
                            )
                            status_filtered = status.copy()
                            outliers_local = (~inliers_mask)
                            if np.any(outliers_local):
                                status_filtered[lk_ok_idx[outliers_local]] = 0
                            status = status_filtered

                        t_track = float(t)

                        features_to_keep = []
                        for i, f in enumerate(features):
                            if i < len(status) and status[i] == 1:
                                nx, ny = new_pts[i]
                                f.x = float(np.clip(nx, 0.0, width - 1))
                                f.y = float(np.clip(ny, 0.0, height - 1))
                                f.last_seen_t = t_track
                                features_to_keep.append(f)
                        features = features_to_keep

                    # top-up delle feature (sempre, anche se non hai prev_ts_frame)
                    
                    features, next_feature_id = ensure_min_features(
                        features=features,
                        last_corner_time_map=last_corner_time_map,
                        current_time=float(t),
                        delta_t=0.05,
                        min_features_target=MIN_FEATURES_TARGET,
                        max_features_total=MAX_FEATURES_TOTAL,
                        min_dist=MIN_DIST_BETWEEN_FEATURES,
                        width=width,
                        height=height,
                        next_feature_id=next_feature_id,
                        use_ts_supplement=True,
                        ts_image=TS_norm,
                        supplement_method="shi-tomasi",
                        harris_block_size=3,
                        harris_k=0.04,
                        shi_quality=0.01,
                        shi_min_dist=6
                    )

                    MAX_TRACK_AGE = 2.5  # seconds
                    t_now = float(t)
                    features = [f for f in features if (t_now - f.last_seen_t) <= MAX_TRACK_AGE]

                    # --------------------------
                    # preintegration + prediction (tutto con GTSAM)
                    # --------------------------
                    t_key = float(t)

                    state_prev = window.key_states_init[window.key_indices[-1]]
                    nav_prev = _state_to_navstate(state_prev)
                    bias_i = _bias_from_state(state_prev)

                    pim_seed = _preintegrate_imu_between(
                        imu_data_all=imu_data,
                        t0=last_key_time,
                        t1=t_key,
                        imu_params_shared=imu_params_shared,
                        bias_i=bias_i
                    )

                    nav_pred = pim_seed.predict(nav_prev, bias_i)
                    init_state_k = _navstate_to_state_dict(nav_pred,
                            bias_accel=state_prev['bias_accel'],
                            bias_gyro=state_prev['bias_gyro']
                        )
                    # Add new global_k
                    window.add_key_state(t_key=t_key, k_idx=global_k, init_state=init_state_k)
                    active_k_list.append(global_k)

                    for f in features:
                        window.add_visual_observation(fid=int(f.id), k_idx=int(global_k), u=float(f.x), v=float(f.y))

                    # Update TS counters and previous frame
                    N_TS += 1
                    t_prev_TS = t_key
                    last_key_time = t_key  # Update for next seed
                    events_since_last_TS = 0
                    prev_ts_frame = TS_norm.copy()
                    global_k += 1

            # Check stop condition for the window
            if stop_condition(N_TS, imu_meas):
                break

            # Move t_curr to next available measurement time
            t_next_events = events[idx_events, 0] if idx_events < len(events) else np.inf
            t_next_imu = imu_data[idx_imu, 0] if idx_imu < len(imu_data) else np.inf
            t_curr = min(t_next_events, t_next_imu)
            
        # --------------------------
        # Close window: ensure IMU slice covers all keys up to last_key_time
        # --------------------------

        while idx_imu < len(imu_data) and imu_data[idx_imu, 0] <= last_key_time:
            idx_imu += 1

        key_order = sorted(window.key_indices, key=lambda k: window.key_times[k])

        t_window_end = float(last_key_time)
        
        # Use full imu_data; _build_preint_segments selects by timestamps (ti,tj)
        preint_segments = _build_preint_segments(
            key_order=key_order,
            key_times=window.key_times,
            imu_data=imu_data,
            imu_params_shared=imu_params_shared,
            key_states_init=window.key_states_init
        )
        
        n_ft = len(window.feature_tracks)
        n_ge2 = sum(
            1 for obs in window.feature_tracks.values()
            if len([(k,u,v) for (k,u,v) in obs if k in window.key_indices]) >= 3
        )
        logger.info(f"Window debug: feature_tracks={n_ft}, tracks>=3_in_window={n_ge2}")

        # Qui viene attivata la visione nella factor graph dopo 2 finestre di solo IMU
        # Se si disabilita la visione, si ottiene una pure IMU odometry con preintegrazione, 
        # senza correzione da parte della fotocamera (quindi deriva più velocemente)
        window.enable_vision = (window_count > 2)
        # window.enable_vision = False
        window.allow_new_landmarks = True

        # Build graph: prior SOLO per window 1
        graph, initial = window.build_graph_and_initial(
            preint_segments=preint_segments,
            imu_noise=imu_noise_for_window,
            covariance_0=cov_0 if is_first_window else None
        )
        
        remove_keys = gtsam.KeyVector()

        logger.info(f"Corners detected in window: {corner}")

        try:
            isam.update(graph, initial, remove_keys)
            result = isam.calculateEstimate()
            isam_factors = isam.getFactorsUnsafe()
            marginals = gtsam.Marginals(isam_factors, result)
            total_err = graph.error(result)
            logger.info(f"  Graph total error: {total_err:.6f}")
            log_factor_error_breakdown(graph, result, logger)
        except Exception as e:
            logger.error(f"[ISAM2] update failed: {e}")
            result = isam.calculateEstimate()
            marginals = None

        if len(key_order) > 0:
            t_last_window = float(window.key_times[key_order[-1]])
        else:
            t_last_window = t_window_end  # fallback

        graph_total_error_history.append((t_last_window, total_err))

        prev_result = result

        # Extract covariance for LAST pose in window
        cov_pose, cov_vel, cov_bias = window.extract_last_pose_covariance(marginals)
        
        last_k = key_order[-1] if len(key_order) > 0 else None
        
        cov_all.append({
            "time": t_window_end,
            "k_idx": last_k,
            "pose_cov": cov_pose,
            "vel_cov": cov_vel,
            "bias_cov": cov_bias
        })

        # Save and extract results
        window.set_last_result(result)

        poses_win = window.extract_optimized_poses()
        feats_win, depths_win = window.extract_optimized_features_and_depths()

        logger.info(f"Window #{window_count} -> poses: {len(poses_win)}, tracks: {len(feats_win)}, depths: {len(depths_win)}")

        optimized_pose_all.extend(poses_win)
        optimized_depths_all.extend(depths_win)
        for fid, info in feats_win.items():
            optimized_features_all[fid] = info

        # ==========================
        # Next window preparation (KEEP OVERLAP)
        # ==========================
        if last_k is None:
            break

        # Keep last overlap_keep keys from this window as the beginning of the next one
        keep_n = max(1, int(overlap_keep))
        key_order = sorted(window.key_indices, key=lambda k: window.key_times[k])
        keep_keys = key_order[-keep_n:]  # e.g. last 2 keys

        # Ensure timer consistency for kept keys (use original timestamps from window.key_times)
        keep_keys = [int(k) for k in keep_keys]
        k0_next = keep_keys[0]
        t0_next = float(window.key_times[k0_next])

        # Prepare init states for kept keys from optimized result
        # (this is critical: seeds are consistent with current ISAM)
        if prev_result is None or (not prev_result.exists(X(k0_next))):
            # Should not happen, but avoid crash
            init_state_next0 = _make_state_dict(p0, q0_xyzw, v0, bias0_acc, bias0_gyro)
        else:
            init_state_next0 = _state_from_isam_result(prev_result, k0_next, b_a0, b_g0)

        # Update IMU slicing start for next window
        # imu_window_start_idx = imu_window_end_idx

        # Reset counters for TS building
        features = []
        prev_ts_frame = None
        events_since_last_TS = 0
        is_first_window = False
        
        # Keep observations within ~6 windows behind the new start key
        keep_after = int(k0_next) - 6 * int(max_window_keys)
        for fid in list(window.feature_tracks.keys()):
            obs = window.feature_tracks[fid]
            obs = [(k, u, v) for (k, u, v) in obs if int(k) >= keep_after]
            if len(obs) == 0:
                del window.feature_tracks[fid]
            else:
                window.feature_tracks[fid] = obs

        # --------------------------
        # Restart the window at k0_next, but RE-ADD the overlap keys (WITH CORRECT TIMES)
        # --------------------------
        old_key_times = dict(window.key_times)   # <-- SAVE before window.start()

        window.start(k0_idx=int(k0_next), t_start=float(old_key_times[k0_next]), init_state=init_state_next0)

        # window.feature_tracks = {}  # local tracks for the new window

        # Re-add remaining overlap keys using optimized states AND original timestamps
        for kk in keep_keys[1:]:
            kk = int(kk)
            if prev_result is None or (not prev_result.exists(X(kk))):
                continue
            init_state_kk = _state_from_isam_result(prev_result, kk, b_a0, b_g0)
            window.add_key_state(
                t_key=float(old_key_times[kk]),   # <-- CRITICAL FIX
                k_idx=kk,
                init_state=init_state_kk
            )

        # Keep last_result for overlap priors etc.
        if prev_result is not None:
            window.set_last_result(prev_result)

        # Preintegration should start from the latest kept key time (correct)
        last_key_time = float(old_key_times[keep_keys[-1]])

        # Move t_curr to next available measurement time
        if idx_events >= len(events) or idx_imu >= len(imu_data):
            break
        t_next_events = events[idx_events, 0] if idx_events < len(events) else np.inf
        t_next_imu = imu_data[idx_imu, 0] if idx_imu < len(imu_data) else np.inf
        t_curr = min(t_next_events, t_next_imu)

    return optimized_pose_all, optimized_features_all, optimized_depths_all, cov_all, graph_total_error_history


if __name__ == "__main__":

    # LINEAR TRAJECTORY DATA PATHS (da modificare in base alla propria traiettoria)
    events_h5 = "D:\\Thesis\\v2e\\output_lin\\DVS640X480_H5.h5"
    imu_csv = "C:\\Users\\marco\\Desktop\\Thesis\\proxsima\\Saved\\Simulation_Marco\\IMU\\Linear_Traj\\RoverIMU.csv"

    # Transformations sensor -> rover frame (stessi valori sul JSON di UE, attenzione al sistema di riferimento!)
    translation_cam = [0.1, 0.0, 1.25]
    rotation_cam = [0.0, 0.0, 0.0]
    translation_imu = [0.2, -0.3, 0.825]
    rotation_imu = [0.0, 0.0, 0.0]

    # Camera and IMU parameters
    cam_params = {
        'width': 640,
        'height': 480,
        'fov_diag': np.deg2rad(70.0)
    }
    imu_params = {
        'g': 3.72,  # Mars gravity
        'sample_rate_hz': 100.0,
        'noise': {
            'accel': 3.4323e-4, 
            'gyro': 2.04e-5
        }, 
        'bias': {
            'accel': "2.942e-3, 2.942e-3, 2.942e-3", 
            'gyro': "4.85e-6, 4.85e-6, 4.85e-6"
        }, 
        'BiasRW': {
            'accel': 3.4323e-4, 
            'gyro': 2.04e-5
        } 
    }

    # Run VIO pipeline
    opt_poses, opt_feats, opt_depths, cov_all, graph_total_error_history = run_vio(
        imu_csv_path=imu_csv,
        events_h5_path=events_h5,
        translation_cam=translation_cam,
        rotation_cam=rotation_cam,
        translation_imu=translation_imu,
        rotation_imu=rotation_imu,
        imu_cfg=imu_params,
        cam_params=cam_params
    )

    # Print pose coordinates every 20 poses
    print("\nPose coordinates (every 20):")
    for idx, (t, p, q, v) in enumerate(opt_poses):
        if idx % 20 == 0:
            print(f"Pose {idx}: t={t:.3f}s p=[{p[0]:.4f}, {p[1]:.4f}, {p[2]:.4f}] v=[{v[0]:.3f}, {v[1]:.3f}, {v[2]:.3f}]")

    logger.info("\n" + "="*60)
    logger.info("VIO Pipeline Completed")
    logger.info(f"  Total optimized poses: {len(opt_poses)}")
    logger.info(f"  Total optimized features (triangulated post-hoc): {len(opt_feats)}")
    logger.info(f"  Total depth estimates: {len(opt_depths)}")
    logger.info(f"  Total covariance entries: {len(cov_all)}")
    logger.info(f"  Total graph error history entries: {len(graph_total_error_history)}")
    logger.info("="*60)

    #REAL TRAJECTORY LOADING
    real_csv = "C:\\Users\\marco\\Desktop\\Thesis\\Codes\\VIO\\linear.csv"

    # Selezione intervallo temporale per confronto (in secondi, in base alla propria traiettoria)
    # t_0 uguale a 5.0 sempre per fase statica iniziale di 5 secondi
    # t_end da scegliere in base alla durata della traiettoria (es. 25 per non farla vedere tutta, 
    # ovviamente anche prima va inserito un t_end se no si processano tutti i dati)
    t_0 = 5.0
    t_end = 25.0

    t_real, real_xyz, real_quats = load_real_trajectory_csv(real_csv)

    # selezione intervallo temporale
    mask = (t_real >= t_0) & (t_real <= t_end)
    t_real = t_real[mask] - t_0
    real_xyz = real_xyz[mask]
    real_quats = real_quats[mask]

    # posizione iniziale (al tempo t0)
    p0 = real_xyz[0]

    # traslazione all'origine
    real_xyz = real_xyz - p0

    VIO_xyz = np.array([p for (_, p, _, _) in opt_poses], dtype=np.float64)
    VIO_times = np.array([t for (t, _, _, _) in opt_poses])
       
    t_final = t_end - t_0

    # ========== PLOT =========

    if graph_total_error_history:
        times, errors = zip(*graph_total_error_history)
        import matplotlib.pyplot as plt
        plt.figure(figsize=(10,6))
        plt.plot(times, errors, 'b-o', linewidth=2)
        plt.xlabel('Time [s]', fontsize=18)
        plt.ylabel('Graph total error', fontsize=18)
        plt.title('Graph total error vs time', fontsize=20)
        plt.grid(True)
        plt.tight_layout()
        plt.show()

    # Plot 2D trajectory with ellipses
    plot_trajectory_xy_with_ellipses(
        opt_poses=opt_poses,
        cov_all=cov_all,  
        gt_xyz=real_xyz,
        n_ellipses=7,
        sigma_scale=3.0,
        title="VIO Trajectory (x-y) with 3σ uncertainty ellipses",
        show=False
    )
    
    # Plot 3D trajectory with tube
    plot_trajectory_3d_with_tube(
        opt_poses=opt_poses,
        cov_all=cov_all,  
        gt_xyz=real_xyz,
        sigma_scale=3.0,
        tube_alpha=0.15,
        n_segments=7,
        title="VIO Trajectory 3D with 3σ uncertainty tube",
        show=False
    )

    # Plot 3D: trajectory + landmarks
    plot_vio_map_3d(
        opt_poses=opt_poses,
        opt_feats=opt_feats,
        opt_depths=opt_depths,
        title="Event-VIO: trajectory + landmarks",
        feat_color_by="Z",
        max_points=5000,
        show=False
    )
    
    plt.show()