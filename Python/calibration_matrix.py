import numpy as np
import gtsam

# ============================================================
# FRAME CONVENTIONS
# ============================================================
# 1. BODY (Rover): Right-Handed, aligned with IMU
#    - X: forward, Y: left, Z: up
#
# 2. IMU: Right-Handed, aligned with body (rotation_imu=[0,0,0])
#    - Same as body frame
#
# 3. CAMERA (visual): RDF (Right-Down-Forward) per OpenCV/eventi
#    - X: right, Y: down, Z: forward (optical axis)
#
# 4. GTSAM: Right-Handed world/body frames
#
# ============================================================

def build_camera_intrinsics(cam_params) -> np.ndarray:
    """
    Build camera intrinsics matrix K (3x3) from FOV-based pinhole model.
    
    Args:
        cam_params: dict with 'width', 'height', 'fov_diag' (radians)
    
    Returns:
        K (3x3 array)
    """
    width = int(cam_params.get('width', 640))
    height = int(cam_params.get('height', 480))
    fov_diag = float(cam_params.get('fov_diag', np.deg2rad(70.0)))
    
    diag = np.sqrt(width**2 + height**2)
    f = diag / (2.0 * np.tan(fov_diag / 2.0))  # focal length in pixels
    
    fx = f * (width / diag)
    fy = f * (height / diag)
    cx = width / 2.0
    cy = height / 2.0
    
    K = np.array([
        [fx, 0.0, cx],
        [0.0, fy, cy],
        [0.0, 0.0, 1.0]
    ], dtype=np.float64)
    
    return K

def build_T_body_sensor(translation: list, rotation: list) -> gtsam.Pose3:
    """
    Build T_body_sensor (body ← sensor) from translation/rotation in body frame.
    
    Args:
        translation: [x, y, z] in body frame (meters)
        rotation: [rx, ry, rz] Rodrigues vector (radians)
    
    Returns:
        gtsam.Pose3 representing transformation body ← sensor
    """
    t = np.asarray(translation, dtype=np.float64).reshape(3)
    r = np.asarray(rotation, dtype=np.float64).reshape(3)
    
    # Rotation from Rodrigues (axis-angle)
    Rot = gtsam.Rot3.Expmap(r)
    
    return gtsam.Pose3(Rot, gtsam.Point3(*t))

def build_T_imu_cam(translation_cam: list, 
                    rotation_cam: list,
                    translation_imu: list,
                    rotation_imu: list,
                    cam_frame: str = "rdf") -> gtsam.Pose3:
    """
    Build T_imu_cam (IMU <-- camera) extrinsics
    
    Args:
        translation_cam: [x,y,z] camera in body frame (FLU coords)
        rotation_cam: [rx,ry,rz] mounting bracket orientation (Rodrigues)
        translation_imu: [x,y,z] IMU in body frame (FLU coords)
        rotation_imu: [rx,ry,rz] IMU orientation (Rodrigues)
        cam_frame: "rdf" (eventi) o "flu"
    
    Returns:
        T_imu_cam: gtsam.Pose3 (IMU frame <-- camera RDF frame)
    """
    # 1. Build T_body_cam directly
    if cam_frame.lower() == "rdf":
        # Rotation: cam RDF → body FLU
        # Mapping:
        #   X_body (forward) = Z_cam (forward in RDF)
        #   Y_body (left)    = -X_cam (right → negate)
        #   Z_body (up)      = -Y_cam (down → negate)
        R_body_from_cam_rdf = np.array([
            [ 0.0,  0.0,  1.0],
            [-1.0,  0.0,  0.0],
            [ 0.0, -1.0,  0.0]
        ], dtype=np.float64)
        
        # Apply mounting bracket rotation
        r_mounting = np.asarray(rotation_cam, dtype=np.float64).reshape(3)
        if np.linalg.norm(r_mounting) > 1e-9:
            R_mounting = gtsam.Rot3.Expmap(r_mounting).matrix()
            R_body_from_cam_rdf = R_mounting @ R_body_from_cam_rdf
        
        # Translation: camera position in body frame (already in FLU coords)
        t_body_cam = np.asarray(translation_cam, dtype=np.float64).reshape(3)
        
        T_body_cam = gtsam.Pose3(gtsam.Rot3(R_body_from_cam_rdf), gtsam.Point3(*t_body_cam))
    else:
        # Camera already in FLU: standard construction
        T_body_cam = build_T_body_sensor(translation_cam, rotation_cam)
    
    # 2. Build T_body_imu (assume aligned: rotation_imu ≈ [0,0,0])
    T_body_imu = build_T_body_sensor(translation_imu, rotation_imu)
    
    # 3. T_imu_cam = T_imu_body ∘ T_body_cam
    T_imu_body = T_body_imu.inverse()
    T_imu_cam = T_imu_body.compose(T_body_cam)
    
    return T_imu_cam