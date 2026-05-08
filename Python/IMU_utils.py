import numpy as np
import gtsam
from typing import Tuple, List, Dict

# Utility functions per IMU, VIO e GTSAM.

def _make_state_dict(p: np.ndarray, q_xyzw: np.ndarray, v: np.ndarray,
                     b_a: np.ndarray, b_g: np.ndarray) -> dict:
    return {
        'p': np.asarray(p, dtype=np.float64).reshape(3),
        'q': np.asarray(q_xyzw, dtype=np.float64).reshape(4),
        'v': np.asarray(v, dtype=np.float64).reshape(3),
        'bias_accel': np.asarray(b_a, dtype=np.float64).reshape(3),
        'bias_gyro': np.asarray(b_g, dtype=np.float64).reshape(3),
    }

def _state_to_navstate(state: dict) -> "gtsam.NavState":
    # Converte il tuo dict state in gtsam.NavState.
    # state['q'] è [x,y,z,w] e rappresenta Rwb (body->world).
    
    p = np.asarray(state['p'], dtype=np.float64).reshape(3)
    v = np.asarray(state['v'], dtype=np.float64).reshape(3)
    q = np.asarray(state['q'], dtype=np.float64).reshape(4)
    Rot = gtsam.Rot3.Quaternion(float(q[3]), float(q[0]), float(q[1]), float(q[2]))  # w,x,y,z
    pose = gtsam.Pose3(Rot, gtsam.Point3(float(p[0]), float(p[1]), float(p[2])))
    return gtsam.NavState(pose, gtsam.Point3(float(v[0]), float(v[1]), float(v[2])))

def _navstate_to_state_dict(nav: "gtsam.NavState", bias_accel: np.ndarray, bias_gyro: np.ndarray) -> dict:
    """
    Converte gtsam.NavState -> dict state.
    Robusta ai diversi binding Python:
      - pose.translation() può essere gtsam.Point3 oppure np.ndarray
      - NavState può avere velocity() (standard) oppure v() (alias)
    """
    pose = nav.pose()

    # --- translation robust ---
    t = pose.translation()
    try:
        # Caso gtsam.Point3
        p = np.array([t.x(), t.y(), t.z()], dtype=np.float64)
    except Exception:
        # Caso np.ndarray / array-like
        p = np.asarray(t, dtype=np.float64).reshape(3)

    # --- velocity robust ---
    vel_obj = None
    if hasattr(nav, "velocity") and callable(getattr(nav, "velocity")):
        vel_obj = nav.velocity()
    elif hasattr(nav, "v") and callable(getattr(nav, "v")):
        vel_obj = nav.v()
    else:
        raise AttributeError("NavState has neither velocity() nor v() in this gtsam binding.")

    try:
        # Caso gtsam.Point3 / Vector3 con x(),y(),z()
        v = np.array([vel_obj.x(), vel_obj.y(), vel_obj.z()], dtype=np.float64)
    except Exception:
        # Caso np.ndarray / array-like
        v = np.asarray(vel_obj, dtype=np.float64).reshape(3)

    # --- quaternion ---
    q = pose.rotation().toQuaternion()
    q_xyzw = np.array([q.x(), q.y(), q.z(), q.w()], dtype=np.float64)

    return {
        'p': p,
        'v': v,
        'q': q_xyzw,
        'bias_accel': np.asarray(bias_accel, dtype=np.float64).reshape(3),
        'bias_gyro': np.asarray(bias_gyro, dtype=np.float64).reshape(3),
    }

def _parse_bias_cfg(bias_cfg) -> Tuple[np.ndarray, np.ndarray]:
    def _to_vec3(x):
        if isinstance(x, (list, tuple, np.ndarray)):
            arr = np.asarray(x, dtype=np.float64).reshape(3)
        elif isinstance(x, str):
            arr = np.asarray([float(v.strip()) for v in x.split(",")], dtype=np.float64).reshape(3)
        else:
            raise ValueError("Invalid bias entry format, expected list/tuple/ndarray or comma-separated string.")
        return arr
    b_a = _to_vec3(bias_cfg.get('accel', [0.05, -0.02, 0.01]))
    b_g = _to_vec3(bias_cfg.get('gyro', [0.002, -0.001, 0.0005]))
    return b_a, b_g

def _bias_from_state(state: dict) -> "gtsam.imuBias.ConstantBias":
    b_a = np.asarray(state['bias_accel'], dtype=np.float64).reshape(3)
    b_g = np.asarray(state['bias_gyro'], dtype=np.float64).reshape(3)
    return gtsam.imuBias.ConstantBias(b_a, b_g)

def _preintegrate_imu_between(
    imu_data_all: np.ndarray,
    t0: float,
    t1: float,
    imu_params_shared: gtsam.PreintegrationParams,
    bias_i: "gtsam.imuBias.ConstantBias"
) -> gtsam.PreintegratedImuMeasurements:
    """
    Preintegra IMU tra [t0, t1] usando dt da timestamp del CSV.
    imu_data_all: (N,7) [t, ax,ay,az,gx,gy,gz] (in RH, body).
    """
    pim = gtsam.PreintegratedImuMeasurements(imu_params_shared, bias_i)

    if t1 <= t0:
        # integra dt minimo per evitare degenerazioni
        pim.integrateMeasurement(np.zeros(3), np.zeros(3), max(t1 - t0, 1e-6))
        return pim

    t_all = imu_data_all[:, 0].astype(np.float64)

    # indici delle misure in (t0, t1]
    idxs = np.where((t_all > t0) & (t_all <= t1))[0]
    
    if idxs.size == 0:
        # nessuna misura dentro l'intervallo: integra zero con dt = (t1-t0)
        pim.integrateMeasurement(np.zeros(3), np.zeros(3), max(t1 - t0, 1e-6))
        return pim

    # Se la prima misura arriva dopo t0, fa un "head" usando la prima misura disponibile
    first = int(idxs[0])
    if t_all[first] > t0:
        dt_head = max(float(t_all[first] - t0), 1e-6)
        acc0 = imu_data_all[first, 1:4].astype(np.float64)
        gyr0 = imu_data_all[first, 4:7].astype(np.float64)
        pim.integrateMeasurement(acc0, gyr0, dt_head)

    # Integra tra misure consecutive nel range
    for k in range(first, int(idxs[-1])):
        t_k = float(t_all[k])
        t_k1 = float(t_all[k + 1])
        if t_k1 <= t0:
            continue
        if t_k >= t1:
            break
        dt = max(min(t_k1, t1) - max(t_k, t0), 1e-6)
        acc = imu_data_all[k, 1:4].astype(np.float64)
        gyr = imu_data_all[k, 4:7].astype(np.float64)
        pim.integrateMeasurement(acc, gyr, dt)

    # Se l'ultima misura è prima di t1, fa una "tail" usando l'ultima misura disponibile nel range
    last = int(idxs[-1])
    if t1 > t_all[last]:
        dt_tail = max(float(t1 - t_all[last]), 1e-6)
        acc_last = imu_data_all[last, 1:4].astype(np.float64)
        gyr_last = imu_data_all[last, 4:7].astype(np.float64)
        pim.integrateMeasurement(acc_last, gyr_last, dt_tail)

    return pim

def _build_preint_segments(key_order: List[int],
                           key_times: Dict[int, float],
                           imu_data: np.ndarray,
                           imu_params_shared: gtsam.PreintegrationParams,
                           key_states_init: Dict[int, dict]) -> List[dict]:
    """
    Costruisce segmenti di preintegrazione IMU tra coppie di keyframe consecutivi in key_order.
    Restituisce lista di dict con chiavi: 'i', 'j', 'ti', 'tj', 'preint' (gtsam.PreintegratedImuMeasurements).
    key_order: lista ordinata di keyframe indices (es. [0, 1, 2, ...])
    key_times: dict k_idx -> timestamp float
    """
    segments: List[dict] = []
    if len(key_order) < 2 or imu_data.size == 0:
        return segments
    t_all = imu_data[:, 0]

    for idx in range(len(key_order) - 1):
        ki = int(key_order[idx])
        kj = int(key_order[idx + 1])
        ti = float(key_times[ki])
        tj = float(key_times[kj])

        st_i = key_states_init.get(ki, None)
        if st_i is not None:
            b_a = np.asarray(st_i['bias_accel'], dtype=np.float64).reshape(3)
            b_g = np.asarray(st_i['bias_gyro'], dtype=np.float64).reshape(3)
        else:
            b_a = np.zeros(3, dtype=np.float64)
            b_g = np.zeros(3, dtype=np.float64)
        bias_i = gtsam.imuBias.ConstantBias(b_a, b_g)

        pim = gtsam.PreintegratedImuMeasurements(imu_params_shared, bias_i)

        idxs = np.where((t_all >= ti) & (t_all <= tj))[0]
        if idxs.size == 0:
            dt0 = max(tj - ti, 1e-3)
            pim.integrateMeasurement(np.zeros(3), np.zeros(3), dt0)
        else:
            t_first = float(t_all[idxs[0]])
            if t_first > ti:
                dt_head = max(t_first - ti, 1e-6)
                axh, ayh, azh = imu_data[idxs[0], 1:4]
                gxh, gyh, gzh = imu_data[idxs[0], 4:7]
                pim.integrateMeasurement(np.array([axh, ayh, azh], dtype=np.float64),
                                         np.array([gxh, gyh, gzh], dtype=np.float64),
                                         dt_head)
            for k in range(idxs[0], idxs[-1]):
                t_k = float(t_all[k])
                t_k1 = float(t_all[k + 1]) if (k + 1) < len(t_all) else t_k
                dt = max(t_k1 - t_k, 1e-6)
                ax, ay, az = imu_data[k, 1:4]
                gx, gy, gz = imu_data[k, 4:7]
                pim.integrateMeasurement(np.array([ax, ay, az], dtype=np.float64),
                                         np.array([gx, gy, gz], dtype=np.float64),
                                         dt)
            t_last = float(t_all[idxs[-1]])
            if tj > t_last:
                dt_tail = max(tj - t_last, 1e-6)
                ax, ay, az = imu_data[idxs[-1], 1:4]
                gx, gy, gz = imu_data[idxs[-1], 4:7]
                pim.integrateMeasurement(np.array([ax, ay, az], dtype=np.float64),
                                         np.array([gx, gy, gz], dtype=np.float64),
                                         dt_tail)

        segments.append({'i': ki, 'j': kj, 'ti': ti, 'tj': tj, 'preint': pim})

    return segments