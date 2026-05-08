import numpy as np
import cv2

# RANSAC per filtrare corrispondenze di flusso ottico (translation/similarity/affine)

def _minimal_required(model: str) -> int:
    if model == "translation":
        return 1
    if model == "similarity":
        return 2  # 4 DoF -> due corrispondenze bastano
    if model == "affine":
        return 3  # 6 DoF -> tre corrispondenze
    raise ValueError(f"Modello RANSAC non supportato: {model}")

def _estimate_translation(p_old: np.ndarray, p_new: np.ndarray):
    # p_old/p_new shape (N,2). Stima come mediana dei vettori di flusso
    flow = p_new - p_old
    t = np.median(flow, axis=0)
    M = np.array([[1.0, 0.0, t[0]], [0.0, 1.0, t[1]]], dtype=np.float64)
    return M

def _residuals_affine_like(M: np.ndarray, p_old: np.ndarray, p_new: np.ndarray) -> np.ndarray:
    # M: 2x3, p_old/p_new: (N,2)
    homog = np.hstack([p_old, np.ones((p_old.shape[0], 1), dtype=p_old.dtype)])
    pred = (M @ homog.T).T
    d = np.linalg.norm(pred - p_new, axis=1)
    return d

def ransac_filter_flows(p_old_xy: np.ndarray,
                        p_new_xy: np.ndarray,
                        model: str = "similarity",
                        ransac_thresh: float = 3.0,
                        max_iters: int = 800,
                        confidence: float = 0.995):
    """
    Seleziona gli inlier tra corrispondenze (p_old_xy -> p_new_xy) con RANSAC.
    Ritorna:
      - inlier_mask (bool, shape N)
      - M (2x3) trasformazione stimata (translation/similarity/affine)
    """
    assert p_old_xy.shape == p_new_xy.shape and p_old_xy.shape[1] == 2
    N = p_old_xy.shape[0]
    if N < _minimal_required(model):
        # Non abbastanza punti: niente filtro
        return np.ones(N, dtype=bool), np.array([[1,0,0],[0,1,0]], dtype=np.float64)

    if model == "translation":
        # RANSAC custom per pura traslazione (1 corrispondenza per campione)
        best_inliers = None
        best_M = None
        rng = np.random.default_rng(12345)
        for _ in range(max_iters):
            i = int(rng.integers(0, N))
            t_vec = p_new_xy[i] - p_old_xy[i]
            M = np.array([[1.0, 0.0, t_vec[0]], [0.0, 1.0, t_vec[1]]], dtype=np.float64)
            res = _residuals_affine_like(M, p_old_xy, p_new_xy)
            inliers = res <= ransac_thresh
            if best_inliers is None or inliers.sum() > best_inliers.sum():
                # rifinisci con mediana sugli inlier correnti
                M_ref = _estimate_translation(p_old_xy[inliers], p_new_xy[inliers])
                res_ref = _residuals_affine_like(M_ref, p_old_xy, p_new_xy)
                inliers_ref = res_ref <= ransac_thresh
                if best_inliers is None or inliers_ref.sum() > best_inliers.sum():
                    best_inliers = inliers_ref
                    best_M = M_ref
            # early stop se consenso alto
            if best_inliers is not None and best_inliers.sum() > 0.9 * N:
                break
        if best_inliers is None:
            best_inliers = np.ones(N, dtype=bool)
            best_M = np.array([[1,0,0],[0,1,0]], dtype=np.float64)
        return best_inliers, best_M

    # Similarity o Affine con OpenCV (già include RANSAC)
    method = cv2.RANSAC
    if model == "similarity":
        M, inliers = cv2.estimateAffinePartial2D(
            p_old_xy, p_new_xy,
            method=method,
            ransacReprojThreshold=ransac_thresh,
            maxIters=int(max_iters),
            confidence=float(confidence),
            refineIters=10
        )
    elif model == "affine":
        M, inliers = cv2.estimateAffine2D(
            p_old_xy, p_new_xy,
            method=method,
            ransacReprojThreshold=ransac_thresh,
            maxIters=int(max_iters),
            confidence=float(confidence),
            refineIters=10
        )
    else:
        raise ValueError(f"Modello RANSAC non supportato: {model}")

    if M is None or inliers is None:
        # Fall-back: nessun modello trovato
        return np.ones(N, dtype=bool), np.array([[1,0,0],[0,1,0]], dtype=np.float64)

    inlier_mask = (inliers.ravel().astype(bool))
    return inlier_mask, M