import numpy as np
import cv2 as cv
from typing import List, Optional, Tuple, Dict

import gtsam
from gtsam.symbol_shorthand import X, L

# Utilities for inverse depth triangulation of landmarks from multiple camera observations and known camera poses.

def make_cal3_s2_from_K(K: np.ndarray) -> gtsam.Cal3_S2:
    K = np.asarray(K, dtype=np.float64).reshape(3, 3)
    fx = float(K[0, 0]); fy = float(K[1, 1]); cx = float(K[0, 2]); cy = float(K[1, 2])
    return gtsam.Cal3_S2(fx, fy, 0.0, cx, cy)

def _triangulate_dlt(P: np.ndarray, Pp: np.ndarray, pts1: np.ndarray, pts2: np.ndarray) -> np.ndarray:
    
    # Linear triangulation (DLT). Returns Nx4 homogeneous 3D points in the WORLD frame
    # when P, Pp are world->cam projection matrices (P = K [R_cw | t_cw]).
    # If P = K [I|0] and Pp = K [R|t], points are in cam1 frame.
    
    N = pts1.shape[0]
    Xs = np.zeros((N, 4), dtype=np.float64)
    for i in range(N):
        x, y = float(pts1[i, 0]), float(pts1[i, 1])
        xp, yp = float(pts2[i, 0]), float(pts2[i, 1])
        A = np.vstack([
            x * P[2] - P[0],
            y * P[2] - P[1],
            xp * Pp[2] - Pp[0],
            yp * Pp[2] - Pp[1],
        ])
        _, _, Vt = np.linalg.svd(A)
        X = Vt[-1]
        Xs[i] = X / (X[-1] if abs(X[-1]) > 1e-15 else 1.0)
    return Xs

def _build_projection_from_cam_pose_cw(T_cam_world: gtsam.Pose3, K: np.ndarray) -> np.ndarray:
    
    # Build 3x4 projection matrix P = K [R_cw | t_cw] from camera<-world extrinsics.
    
    R_cw = np.array(T_cam_world.rotation().matrix(), dtype=np.float64)
    t_cw = np.array(T_cam_world.translation(), dtype=np.float64).reshape(3, 1)
    Rt = np.hstack([R_cw, t_cw])
    return K @ Rt

def select_best_observation_pair(obs_list: List[Tuple[int, float, float]],
                                 cam_poses_cw: Dict[int, gtsam.Pose3],
                                 K: np.ndarray) -> Optional[Tuple[Tuple[int, float, float], Tuple[int, float, float]]]:
    
    # Choose a pair of observations with the largest baseline / ray angle for robust two-view triangulation.
    
    if len(obs_list) < 2:
        return None
    # Precompute normalized rays in camera frames
    rays = {}
    Kinv = np.linalg.inv(K)
    for (k, u, v) in obs_list:
        pix = np.array([u, v, 1.0], dtype=np.float64)
        ray = Kinv @ pix
        n = np.linalg.norm(ray)
        if n < 1e-12:
            rays[(k, u, v)] = None
        else:
            rays[(k, u, v)] = ray / n
    best = None
    best_score = -np.inf
    for i in range(len(obs_list)):
        for j in range(i+1, len(obs_list)):
            o1 = obs_list[i]; o2 = obs_list[j]
            r1 = rays.get(o1, None); r2 = rays.get(o2, None)
            if r1 is None or r2 is None:
                continue
            # Bring r2 to cam1 frame using relative rotation R_21 = R_cw1 * R_wc2
            Tcw1 = cam_poses_cw[o1[0]]
            Tcw2 = cam_poses_cw[o2[0]]
            R_cw1 = np.array(Tcw1.rotation().matrix(), dtype=np.float64)   # world->cam1
            R_wc2 = np.array(Tcw2.inverse().rotation().matrix(), dtype=np.float64)  # cam2->world
            R_21 = R_cw1 @ R_wc2
            r2_in_1 = (R_21 @ r2.reshape(3,1)).reshape(3)
            cosang = np.clip(np.dot(r1, r2_in_1), -1.0, 1.0)
            ang = np.degrees(np.arccos(cosang))
            if ang > best_score:
                best_score = ang
                best = (o1, o2)
    return best

def triangulate_landmark_with_cam_poses(obs_list: List[Tuple[int, float, float]],
                                        cam_poses_cw: Dict[int, gtsam.Pose3],
                                        K: np.ndarray,
                                        max_reproj_err_px: float = 4.0,
                                        min_angle_deg: float = 0.2) -> Optional[np.ndarray]:
    
    # Triangulate a landmark in WORLD coordinates using two observations and known camera poses.
    # obs_list: [(k_idx, u, v), ...]
    # cam_poses_cw: k_idx -> T_cam_world (Pose3) [cam <- world]
    # Returns world XYZ or None if fails.
    
    if len(obs_list) < 2:
        return None

    pair = select_best_observation_pair(obs_list, cam_poses_cw, K)
    if pair is None:
        return None
    (k1, u1, v1), (k2, u2, v2) = pair

    Tcw1 = cam_poses_cw[int(k1)]
    Tcw2 = cam_poses_cw[int(k2)]
    P1 = _build_projection_from_cam_pose_cw(Tcw1, K)
    P2 = _build_projection_from_cam_pose_cw(Tcw2, K)

    pts1 = np.array([[u1, v1]], dtype=np.float64)
    pts2 = np.array([[u2, v2]], dtype=np.float64)

    # DLT returns WORLD coordinates when using P = K[R_cw|t_cw]
    Xw_h = _triangulate_dlt(P1, P2, pts1, pts2)   # homogeneous in WORLD frame
    Xw = Xw_h[0, :3]

    # Cheirality in cam1 & cam2
    R_cw1 = np.array(Tcw1.rotation().matrix(), dtype=np.float64)
    t_cw1 = np.array(Tcw1.translation(), dtype=np.float64)
    R_cw2 = np.array(Tcw2.rotation().matrix(), dtype=np.float64)
    t_cw2 = np.array(Tcw2.translation(), dtype=np.float64)

    Xc1 = R_cw1 @ Xw + t_cw1
    Xc2 = R_cw2 @ Xw + t_cw2
    if Xc1[2] <= 1e-8 or Xc2[2] <= 1e-8:
        return None

    # Reprojection error
    Xw_h = np.hstack([Xw, 1.0]).reshape(4,1)
    proj1 = (P1 @ Xw_h).reshape(3)
    proj2 = (P2 @ Xw_h).reshape(3)
    u1p, v1p = proj1[0]/proj1[2], proj1[1]/proj1[2]
    u2p, v2p = proj2[0]/proj2[2], proj2[1]/proj2[2]
    err = np.sqrt((u1p - u1)**2 + (v1p - v1)**2 + (u2p - u2)**2 + (v2p - v2)**2)
    if err > float(max_reproj_err_px):
        return None

    # Ray angle check using R_21 = R_cw1 * R_wc2
    R_wc2 = np.array(Tcw2.inverse().rotation().matrix(), dtype=np.float64)
    R_21 = R_cw1 @ R_wc2
    Kinv = np.linalg.inv(K)
    r1 = Kinv @ np.array([u1, v1, 1.0], dtype=np.float64)
    r2 = Kinv @ np.array([u2, v2, 1.0], dtype=np.float64)
    r1 /= max(np.linalg.norm(r1), 1e-12)
    r2 /= max(np.linalg.norm(r2), 1e-12)
    r2_in_1 = R_21 @ r2
    cosang = np.clip(np.dot(r1, r2_in_1), -1.0, 1.0)
    ang = np.degrees(np.arccos(cosang))
    if ang < float(min_angle_deg):
        return None

    return Xw
