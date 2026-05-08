import numpy as np
import gtsam
from typing import Dict, List, Tuple, Optional
from gtsam.symbol_shorthand import X, V, B, L

from inverse_depth import make_cal3_s2_from_K, triangulate_landmark_with_cam_poses

# Utilities for building the sliding window graph with persistent landmarks and pose priors.
# Quasi tutte le funzioni usano GTSAM, quindi consiglio vivamente di leggere le varie funzioni di GTSAM 
# per capire bene cosa fanno i vari fattori, noise model, etc.

def _quat_xyzw_to_wxyz(q_xyzw: np.ndarray) -> tuple[float, float, float, float]:
    q = np.asarray(q_xyzw, dtype=np.float64).reshape(4)
    return float(q[3]), float(q[0]), float(q[1]), float(q[2])


def make_pose3_from_state(state: dict) -> gtsam.Pose3:
    p = np.asarray(state["p"], dtype=np.float64).reshape(3)
    q = np.asarray(state["q"], dtype=np.float64).reshape(4)
    w, x, y, z = _quat_xyzw_to_wxyz(q)
    return gtsam.Pose3(gtsam.Rot3.Quaternion(w, x, y, z), gtsam.Point3(*p))


def get_bias_from_state(state: dict) -> gtsam.imuBias.ConstantBias:
    b_a = np.asarray(state["bias_accel"], dtype=np.float64).reshape(3)
    b_g = np.asarray(state["bias_gyro"], dtype=np.float64).reshape(3)
    return gtsam.imuBias.ConstantBias(b_a, b_g)


def get_velocity_vector_from_state(state: dict) -> np.ndarray:
    return np.asarray(state["v"], dtype=np.float64).reshape(3)


class VIOWindow:
    """
    Persistent-landmark sliding-window graph builder.

    - Landmarks are keyed by feature id: L(fid).
    - New landmarks are created only when allow_new_landmarks=True.
    - Existing landmarks are reused across windows via last_result (ISAM2 estimate).
    - Outlier protection:
        * robust noise
        * reprojection gate per observation
        * limit to max 3 obs per landmark per window
    - Optional overlap pose priors to prevent ill-conditioned windows.
    """

    def __init__(self,
                 T_cam_imu_pose3: gtsam.Pose3,
                 K: np.ndarray,
                 meas_sigma_px: float = 8.0):

        self.T_cam_imu = T_cam_imu_pose3
        self.K = np.asarray(K, dtype=np.float64).reshape(3, 3)
        self.cal = make_cal3_s2_from_K(self.K)
        self.meas_sigma_px = float(meas_sigma_px)

        # Window storage
        self.key_indices: List[int] = []
        self.key_times: Dict[int, float] = {}
        self.key_states_init: Dict[int, dict] = {}
        # Tracks: fid -> [(k,u,v), ...] (global k indices)
        self.feature_tracks: Dict[int, List[Tuple[int, float, float]]] = {}
        self.last_result: Optional[gtsam.Values] = None

        # ---- controls ----
        self.enable_vision: bool = True
        self.allow_new_landmarks: bool = True

        # Stabilize overlap (soft pose priors on last few keys using last_result)
        self.add_overlap_pose_priors: bool = True
        self.max_pose_priors_per_window: int = 2
        self.overlap_pose_prior_sigma_rot_deg: float = 3.0
        self.overlap_pose_prior_sigma_pos_m: float = 0.5

        # ---- triangulation gates for NEW landmarks only ----
        self.seed_max_reproj_err_px: float = 2.5
        self.seed_min_angle_deg: float = 1.0
        self.seed_max_depth_m: float = 60.0

        # Soft prior for NEW landmarks (helps conditioning)
        self.lm_prior_sigma_m: float = 25.0

        # Visual selection
        self.min_obs_existing_landmark: int = 2
        self.min_obs_new_landmark: int = 3
        self.min_baseline_new_s: float = 0.05

        # Limit how many we add per window
        self.max_existing_landmarks_per_window: int = 2000
        self.max_new_landmarks_per_window: int = 200

        # Reprojection gating
        self.reproj_gate_px_existing: float = 10
        self.reproj_gate_px_new: float = 6.0
        self.min_inlier_obs_existing: int = 2
        self.min_inlier_obs_new: int = 3

        # Limit obs per landmark inside a window
        self.max_obs_per_landmark_per_window: int = 10

    # --------------------------
    # Window interface
    # --------------------------

    def start(self, k0_idx: int, t_start: float, init_state: dict):
        k0 = int(k0_idx)
        self.key_indices = [k0]
        self.key_times = {k0: float(t_start)}
        self.key_states_init = {k0: init_state}

    def add_key_state(self, t_key: float, k_idx: int, init_state: dict):
        k = int(k_idx)
        if k not in self.key_indices:
            self.key_indices.append(k)
        self.key_times[k] = float(t_key)
        self.key_states_init[k] = init_state

    def add_visual_observation(self, fid: int, k_idx: int, u: float, v: float):
        self.feature_tracks.setdefault(int(fid), []).append((int(k_idx), float(u), float(v)))

    def set_last_result(self, result: gtsam.Values):
        self.last_result = result

    # --------------------------
    # Priors helpers
    # --------------------------

    def _add_priors(self, graph: gtsam.NonlinearFactorGraph, key0: int, mean: dict, covariance: dict):
        pose_prior_noise = gtsam.noiseModel.Gaussian.Covariance(covariance["pose"])
        graph.push_back(gtsam.PriorFactorPose3(X(key0), mean["pose"], pose_prior_noise))

        vel_prior_noise = gtsam.noiseModel.Gaussian.Covariance(covariance["vel"])
        graph.push_back(gtsam.PriorFactorVector(V(key0), mean["vel"], vel_prior_noise))

        bias_prior_noise = gtsam.noiseModel.Gaussian.Covariance(covariance["bias"])
        graph.push_back(gtsam.PriorFactorConstantBias(B(key0), mean["bias"], bias_prior_noise))

    def _add_overlap_pose_priors_from_last_result(self, graph: gtsam.NonlinearFactorGraph):
        if not self.add_overlap_pose_priors or self.last_result is None:
            return
        if len(self.key_indices) == 0:
            return

        ordered = sorted(self.key_indices, key=lambda k: self.key_times.get(int(k), 0.0))
        use = ordered[-int(self.max_pose_priors_per_window):]

        sig_rot = np.deg2rad(float(self.overlap_pose_prior_sigma_rot_deg))
        sig_pos = float(self.overlap_pose_prior_sigma_pos_m)
        noise = gtsam.noiseModel.Diagonal.Sigmas(
            np.array([sig_rot, sig_rot, sig_rot, sig_pos, sig_pos, sig_pos], dtype=np.float64)
        )

        for k in use:
            k = int(k)
            if self.last_result.exists(X(k)):
                pose = self.last_result.atPose3(X(k))
                graph.push_back(gtsam.PriorFactorPose3(X(k), pose, noise))

    def _bias_between_noise(self, imu_noise: dict, dt: float):
        sigma_ba = float(imu_noise.get("accel_bias", 1e-4))
        sigma_bg = float(imu_noise.get("gyro_bias", 1e-5))
        s_ba = sigma_ba * np.sqrt(max(dt, 1e-9))
        s_bg = sigma_bg * np.sqrt(max(dt, 1e-9))
        return gtsam.noiseModel.Diagonal.Sigmas(
            np.array([s_ba, s_ba, s_ba, s_bg, s_bg, s_bg], dtype=np.float64)
        )

    # --------------------------
    # Pose utilities (cam_T_world)
    # --------------------------

    def _get_Tcw_seed(self, k_idx: int) -> Optional[gtsam.Pose3]:
        st = self.key_states_init.get(int(k_idx), None)
        if st is None:
            return None
        T_wi = make_pose3_from_state(st)       # world_T_imu
        T_wc = T_wi.compose(self.T_cam_imu)    # world_T_cam
        return T_wc.inverse()                  # cam_T_world

    def _get_Tcw_best_available(self, k_idx: int) -> Optional[gtsam.Pose3]:
        k_idx = int(k_idx)
        if self.last_result is not None and self.last_result.exists(X(k_idx)):
            T_wi = self.last_result.atPose3(X(k_idx))
            T_wc = T_wi.compose(self.T_cam_imu)
            return T_wc.inverse()
        return self._get_Tcw_seed(k_idx)

    # --------------------------
    # Reprojection
    # --------------------------

    def _project_uv(self, Tcw: gtsam.Pose3, X_world: np.ndarray) -> Tuple[float, float, float]:
        Xw = np.asarray(X_world, dtype=np.float64).reshape(3)
        R_cw = np.array(Tcw.rotation().matrix(), dtype=np.float64)
        t_cw = np.array(Tcw.translation(), dtype=np.float64).reshape(3)
        Xc = R_cw @ Xw + t_cw
        Z = float(Xc[2])

        if Z <= 1e-12:
            return float("nan"), float("nan"), Z

        fx = float(self.K[0, 0])
        fy = float(self.K[1, 1])
        cx = float(self.K[0, 2])
        cy = float(self.K[1, 2])
        u = fx * (Xc[0] / Z) + cx
        v = fy * (Xc[1] / Z) + cy
        return float(u), float(v), Z

    def _reproj_err_px(self, Tcw: gtsam.Pose3, X_world: np.ndarray, u_meas: float, v_meas: float) -> float:
        u_pred, v_pred, Z = self._project_uv(Tcw, X_world)
        if not np.isfinite(u_pred) or not np.isfinite(v_pred) or Z <= 1e-12:
            return float("inf")
        return float(np.hypot(u_pred - float(u_meas), v_pred - float(v_meas)))

    def _filter_obs_by_reprojection(self,
                                    X_world: np.ndarray,
                                    obs_list: List[Tuple[int, float, float]],
                                    gate_px: float) -> List[Tuple[int, float, float]]:
        inliers: List[Tuple[int, float, float]] = []
        for (k_idx, u, v) in obs_list:
            Tcw = self._get_Tcw_best_available(int(k_idx))
            if Tcw is None:
                continue
            err = self._reproj_err_px(Tcw, X_world, u, v)
            if err <= float(gate_px):
                inliers.append((int(k_idx), float(u), float(v)))
        return inliers

    def _select_obs_subset(self, obs_inliers: List[Tuple[int, float, float]]) -> List[Tuple[int, float, float]]:
        if len(obs_inliers) <= int(self.max_obs_per_landmark_per_window):
            return obs_inliers
        obs_sorted = sorted(obs_inliers, key=lambda o: self.key_times.get(int(o[0]), 0.0))
        if int(self.max_obs_per_landmark_per_window) <= 2:
            return [obs_sorted[0], obs_sorted[-1]]
        mid = obs_sorted[len(obs_sorted) // 2]
        return [obs_sorted[0], mid, obs_sorted[-1]]

    # --------------------------
    # Triangulation for NEW landmarks
    # --------------------------

    def _triangulate_landmark_initial(self, obs_list: List[Tuple[int, float, float]]) -> Optional[np.ndarray]:
        if len(obs_list) < 2:
            return None

        cam_poses_cw: Dict[int, gtsam.Pose3] = {}
        for (k_idx, _u, _v) in obs_list:
            Tcw = self._get_Tcw_seed(int(k_idx))
            if Tcw is None:
                continue
            cam_poses_cw[int(k_idx)] = Tcw
        if len(cam_poses_cw) < 2:
            return None

        X_world = triangulate_landmark_with_cam_poses(
            obs_list=obs_list,
            cam_poses_cw=cam_poses_cw,
            K=self.K,
            max_reproj_err_px=float(self.seed_max_reproj_err_px),
            min_angle_deg=float(self.seed_min_angle_deg),
        )
        if X_world is None:
            return None

        X_world = np.asarray(X_world, dtype=np.float64).reshape(3)
        if not np.all(np.isfinite(X_world)):
            return None

        # Depth sanity check in cam of first obs
        k0 = int(obs_list[0][0])
        Tcw0 = cam_poses_cw.get(k0, None) or next(iter(cam_poses_cw.values()))
        R_cw0 = np.array(Tcw0.rotation().matrix(), dtype=np.float64)
        t_cw0 = np.array(Tcw0.translation(), dtype=np.float64).reshape(3)
        Z = float((R_cw0 @ X_world + t_cw0)[2])
        if Z <= 1e-6 or Z > float(self.seed_max_depth_m):
            return None

        return X_world

    # --------------------------
    # Graph builder
    # --------------------------

    def build_graph_and_initial(
        self,
        preint_segments: List[dict],
        imu_noise: dict,
        covariance_0: Optional[dict] = None
    ) -> Tuple[gtsam.NonlinearFactorGraph, gtsam.Values]:

        graph = gtsam.NonlinearFactorGraph()
        initial = gtsam.Values()
        is_first_window = (self.last_result is None)

        # Initial values for pose/vel/bias
        for k in self.key_indices:
            k = int(k)
            st = self.key_states_init[k]
            key_exists = False
            if not is_first_window:
                key_exists = (
                    self.last_result.exists(X(k)) and
                    self.last_result.exists(V(k)) and
                    self.last_result.exists(B(k))
                )
            if not key_exists:
                initial.insert(X(k), make_pose3_from_state(st))
                initial.insert(V(k), get_velocity_vector_from_state(st))
                initial.insert(B(k), get_bias_from_state(st))

        # Priors only first window
        if covariance_0 is not None:
            k0 = int(self.key_indices[0])
            st0 = self.key_states_init[k0]
            mean_0 = {
                "pose": make_pose3_from_state(st0),
                "vel": get_velocity_vector_from_state(st0),
                "bias": get_bias_from_state(st0),
            }
            self._add_priors(graph, k0, mean_0, covariance_0)

        # Stabilize overlap
        self._add_overlap_pose_priors_from_last_result(graph)

        # IMU factors
        for seg in preint_segments:
            ki = int(seg["i"])
            kj = int(seg["j"])
            pim = seg["preint"]
            graph.push_back(gtsam.ImuFactor(X(ki), V(ki), X(kj), V(kj), B(ki), pim))

        # Bias between factors
        for seg in preint_segments:
            ki = int(seg["i"])
            kj = int(seg["j"])
            ti = float(seg["ti"])
            tj = float(seg["tj"])
            dt = max(tj - ti, 1e-6)
            graph.push_back(gtsam.BetweenFactorConstantBias(
                B(ki), B(kj),
                gtsam.imuBias.ConstantBias(),
                self._bias_between_noise(imu_noise, dt)
            ))

        if not self.enable_vision:
            return graph, initial

        # Visual noise (robust)
        meas_base = gtsam.noiseModel.Isotropic.Sigma(2, float(self.meas_sigma_px))
        
        meas_noise = gtsam.noiseModel.Robust.Create(
            gtsam.noiseModel.mEstimator.Huber.Create(1.345),
            meas_base
        )
        """
        meas_noise = gtsam.noiseModel.Robust.Create(
            gtsam.noiseModel.mEstimator.Tukey.Create(4.685),
            meas_base
        )
        """

        lm_prior_noise = gtsam.noiseModel.Isotropic.Sigma(3, float(self.lm_prior_sigma_m))

        key_set = set(int(k) for k in self.key_indices)

        existing_candidates: List[Tuple[float, int, List[Tuple[int, float, float]]]] = []
        new_candidates: List[Tuple[float, int, List[Tuple[int, float, float]]]] = []

        # Partition into existing vs new
        for fid, obs_list in self.feature_tracks.items():
            fid = int(fid)
            obs_in_window = [(int(k), float(u), float(v)) for (k, u, v) in obs_list if int(k) in key_set]
            if len(obs_in_window) < 2:
                continue

            exists = (self.last_result is not None and self.last_result.exists(L(fid)))

            if exists:
                if len(obs_in_window) < int(self.min_obs_existing_landmark):
                    continue
                score = float(len(obs_in_window))
                existing_candidates.append((score, fid, obs_in_window))
            else:
                if not self.allow_new_landmarks:
                    continue
                if len(obs_in_window) < int(self.min_obs_new_landmark):
                    continue
                k_first = int(obs_in_window[0][0])
                k_last = int(obs_in_window[-1][0])
                t_first = self.key_times.get(k_first, None)
                t_last = self.key_times.get(k_last, None)
                baseline = 0.0 if (t_first is None or t_last is None) else float(t_last - t_first)
                if baseline < float(self.min_baseline_new_s):
                    continue
                score = float(len(obs_in_window)) * baseline
                new_candidates.append((score, fid, obs_in_window))

        existing_candidates.sort(key=lambda x: x[0], reverse=True)
        new_candidates.sort(key=lambda x: x[0], reverse=True)
        existing_candidates = existing_candidates[:int(self.max_existing_landmarks_per_window)]
        new_candidates = new_candidates[:int(self.max_new_landmarks_per_window)]

        # Existing landmarks: reuse last_result landmark position + gated obs
        if self.last_result is not None:
            for _score, fid, obs_in_window in existing_candidates:
                Xw_obj = self.last_result.atPoint3(L(fid))
                try:
                    X_world = np.array([Xw_obj.x(), Xw_obj.y(), Xw_obj.z()], dtype=np.float64)
                except Exception:
                    X_world = np.asarray(Xw_obj, dtype=np.float64).reshape(3)

                obs_inliers = self._filter_obs_by_reprojection(
                    X_world, obs_in_window, self.reproj_gate_px_existing
                )
                if len(obs_inliers) < int(self.min_inlier_obs_existing):
                    continue

                obs_use = self._select_obs_subset(obs_inliers)
                for (k_idx, u, v) in obs_use:
                    graph.push_back(gtsam.GenericProjectionFactorCal3_S2(
                        gtsam.Point2(float(u), float(v)),
                        meas_noise,
                        X(int(k_idx)),
                        L(int(fid)),
                        self.cal,
                        self.T_cam_imu
                    ))

        # New landmarks: triangulate + prior + projection
        for _score, fid, obs_in_window in new_candidates:
            X_seed = self._triangulate_landmark_initial(obs_in_window)
            if X_seed is None:
                continue

            obs_inliers = self._filter_obs_by_reprojection(
                X_seed, obs_in_window, self.reproj_gate_px_new
            )
            if len(obs_inliers) < int(self.min_inlier_obs_new):
                continue

            obs_use = self._select_obs_subset(obs_inliers)
            if len(obs_use) < int(self.min_inlier_obs_new):
                continue

            # init landmark if not present anywhere
            if (not initial.exists(L(fid))) and (self.last_result is None or not self.last_result.exists(L(fid))):
                initial.insert(L(fid), gtsam.Point3(float(X_seed[0]), float(X_seed[1]), float(X_seed[2])))

            # soft prior (only when first created in this graph)
            graph.push_back(gtsam.PriorFactorPoint3(
                L(int(fid)),
                gtsam.Point3(float(X_seed[0]), float(X_seed[1]), float(X_seed[2])),
                lm_prior_noise
            ))

            for (k_idx, u, v) in obs_use:
                graph.push_back(gtsam.GenericProjectionFactorCal3_S2(
                    gtsam.Point2(float(u), float(v)),
                    meas_noise,
                    X(int(k_idx)),
                    L(int(fid)),
                    self.cal,
                    self.T_cam_imu
                ))

        return graph, initial

    # --------------------------
    # Outputs
    # --------------------------

    def extract_optimized_poses(self) -> List[Tuple[float, np.ndarray, np.ndarray, np.ndarray]]:
        if self.last_result is None:
            return []
        out: List[Tuple[float, np.ndarray, np.ndarray, np.ndarray]] = []
        ordered = sorted(self.key_indices, key=lambda k: self.key_times.get(int(k), 0.0))
        for k in ordered:
            k = int(k)
            if not self.last_result.exists(X(k)):
                continue
            pose_k = self.last_result.atPose3(X(k))
            p = np.array(pose_k.translation(), dtype=np.float64)
            q = pose_k.rotation().toQuaternion()
            q_xyzw = np.array([q.x(), q.y(), q.z(), q.w()], dtype=np.float64)
            try:
                v = np.array(self.last_result.atVector(V(k)), dtype=np.float64).reshape(3)
            except Exception:
                v = np.zeros(3, dtype=np.float64)
            out.append((float(self.key_times[k]), p, q_xyzw, v))
        return out

    def extract_optimized_features_and_depths(self) -> Tuple[Dict[int, dict], List[Tuple[int, float]]]:
        """
        Persistent landmarks: return those present in last_result.
        rho is approximated as inverse depth in the camera of the first observation we find.
        """
        if self.last_result is None:
            return {}, []

        feats: Dict[int, dict] = {}
        depths: List[Tuple[int, float]] = []

        for fid, obs_list in self.feature_tracks.items():
            fid = int(fid)
            if not self.last_result.exists(L(fid)):
                continue

            Xw_obj = self.last_result.atPoint3(L(fid))
            try:
                X_world = np.array([Xw_obj.x(), Xw_obj.y(), Xw_obj.z()], dtype=np.float64)
            except Exception:
                X_world = np.asarray(Xw_obj, dtype=np.float64).reshape(3)

            # pick one observation to compute a rough inverse depth
            any_k = None
            for (k_idx, _u, _v) in obs_list:
                any_k = int(k_idx)
                break
            if any_k is None:
                continue

            Tcw = self._get_Tcw_best_available(any_k)
            if Tcw is None:
                continue
            R_cw = np.array(Tcw.rotation().matrix(), dtype=np.float64)
            t_cw = np.array(Tcw.translation(), dtype=np.float64).reshape(3)
            Z = float((R_cw @ X_world + t_cw)[2])
            if Z <= 1e-6:
                continue
            rho = 1.0 / Z

            feats[fid] = {"X_world": X_world, "rho": float(rho)}
            depths.append((fid, float(rho)))

        return feats, depths

    def extract_last_pose_covariance(self, marginals: gtsam.Marginals) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        if marginals is None:
            return np.eye(6) * 1e6, np.eye(3) * 1e6, np.eye(6) * 1e6
        if len(self.key_indices) == 0:
            return np.eye(6), np.eye(3), np.eye(6)

        k_last = int(self.key_indices[-1])

        try:
            cov_pose = marginals.marginalCovariance(X(k_last))
        except Exception:
            cov_pose = np.eye(6) * 1e6

        try:
            cov_vel = marginals.marginalCovariance(V(k_last))
        except Exception:
            cov_vel = np.eye(3) * 1e6

        try:
            cov_bias = marginals.marginalCovariance(B(k_last))
        except Exception:
            cov_bias = np.eye(6) * 1e6

        return cov_pose, cov_vel, cov_bias