import numpy as np
import cv2

# Contine la classe Feature e funzioni per tracking e supplement conrners.

class Feature:
    __slots__ = ("id", "x", "y", "last_seen_t", "last_updated_interval")
    def __init__(self, fid: int, x: float, y: float, t: float, interval_idx: int):
        self.id = fid
        self.x = x
        self.y = y
        self.last_seen_t = t
        self.last_updated_interval = interval_idx

def lk_track_batch(prev_img, curr_img, pts_prev,
                   win_size=(21,21),
                   max_level=2,
                   criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 50, 1e-3)):
    """
    Wrapper calcOpticalFlowPyrLK. pts_prev shape (N,1,2).
    Ritorna new_pts, status, err.
    """
    new_pts, status, err = cv2.calcOpticalFlowPyrLK(
        prev_img, curr_img, pts_prev, None,
        winSize=win_size,
        maxLevel=max_level,
        criteria=criteria,
        flags=0
    )
    if new_pts is None:
        new_pts = pts_prev.copy()
        status = np.zeros((pts_prev.shape[0], 1), dtype=np.uint8)
        err = np.zeros((pts_prev.shape[0], 1), dtype=np.float32)
    return new_pts, status, err

def _supplement_corners_ts(ts_image,
                           method="shi-tomasi",
                           max_add=800,
                           quality=0.01,
                           min_dist=6,
                           harris_block_size=3,
                           harris_k=0.04):
    """
    Estrae corner dal frame TS per arricchire (sempre con metodi tradizionali).
    """
    h, w = ts_image.shape
    pts = []
    if method == "shi-tomasi":
        gft = cv2.goodFeaturesToTrack(ts_image, mask=None,
                                      maxCorners=max_add,
                                      qualityLevel=quality,
                                      minDistance=min_dist,
                                      blockSize=7,
                                      useHarrisDetector=False)
        if gft is not None:
            for p in gft:
                x, y = p[0]
                pts.append((int(round(x)), int(round(y))))
    elif method == "harris":
        harris_resp = cv2.cornerHarris(ts_image, blockSize=harris_block_size, ksize=3, k=harris_k)
        harris_norm = cv2.normalize(harris_resp, None, 0, 255, cv2.NORM_MINMAX)
        thresh = np.percentile(harris_norm, 99.0)
        ys, xs = np.where(harris_norm >= thresh)
        for x, y in zip(xs, ys):
            pts.append((int(x), int(y)))
    else:
        return []
    return pts

def ensure_min_features(features,
                        last_corner_time_map,
                        current_time,
                        delta_t,
                        min_features_target,
                        max_features_total,
                        min_dist,
                        width,
                        height,
                        next_feature_id,
                        use_ts_supplement,
                        ts_image,
                        supplement_method,
                        harris_block_size,
                        harris_k,
                        shi_quality,
                        shi_min_dist):
    """
    Aggiunge nuove feature dai corner recenti + supplement se necessario.
    """
    # Corner recenti (event-based)
    mask_recent = (current_time - last_corner_time_map) < delta_t
    coords_recent = np.argwhere(mask_recent)
    # Limita se eccessivo
    if coords_recent.shape[0] > (max_features_total * 2):
        coords_recent = coords_recent[:(max_features_total * 2)]

    def too_close_to_existing(cx, cy):
        for f in features:
            if abs(f.x - cx) <= min_dist and abs(f.y - cy) <= min_dist:
                return True
        return False

    # Aggiungi dai corner recenti se sotto target
    if len(features) < min_features_target:
        for (ry, rx) in coords_recent:  # attenzione: argwhere output [row(y), col(x)]
            if len(features) >= max_features_total:
                break
            if too_close_to_existing(rx, ry):
                continue
            features.append(Feature(next_feature_id, float(rx), float(ry), current_time, interval_idx=0))
            next_feature_id += 1
            if len(features) >= min_features_target:
                break

    # Supplement se ancora pochi
    if use_ts_supplement and len(features) < min_features_target:
        supp_pts = _supplement_corners_ts(
            ts_image,
            method=supplement_method,
            max_add=min_features_target - len(features),
            quality=shi_quality,
            min_dist=shi_min_dist,
            harris_block_size=harris_block_size,
            harris_k=harris_k
        )
        for (sx, sy) in supp_pts:
            if len(features) >= max_features_total:
                break
            if too_close_to_existing(sx, sy):
                continue
            features.append(Feature(next_feature_id, float(sx), float(sy), current_time, interval_idx=0))
            next_feature_id += 1

    return features, next_feature_id