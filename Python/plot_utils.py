import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Ellipse
from mpl_toolkits.mplot3d import Axes3D
from typing import List, Tuple, Dict, Optional
from scipy.spatial.transform import Rotation as R, Slerp

# Funzioni per plot 2D/3D con ellissi/tube di confidenza basati sulla covariance.

def load_real_trajectory_csv(csv_path: str,
                             delimiter: str = ",",
                             skip_header: int = 1) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Carica CSV ground truth: Time, x, y, z, ...
    Returns: (times, xyz, quaternions) - quaternions è None se non presenti
    """
    data = np.genfromtxt(
        csv_path,
        delimiter=delimiter,
        skip_header=skip_header,
        dtype=np.float64
    )

    if data.ndim == 1:
        data = data.reshape(1, -1)

    if data.shape[1] < 4:
        raise ValueError(
            f"CSV trajectory must have at least 4 columns (Time + x,y,z). "
            f"Found shape={data.shape}"
        )

    t = data[:, 0]
    xyz = data[:, 1:4]
    quat = data[:, 4:8] if data.shape[1] >= 8 else None
    return t, xyz, quat

def _set_axes_equal_3d(ax):
    """
    Imposta scaling uguale su x,y,z per un ax 3D.
    """
    xlim = ax.get_xlim3d()
    ylim = ax.get_ylim3d()
    zlim = ax.get_zlim3d()

    xr = abs(xlim[1] - xlim[0])
    yr = abs(ylim[1] - ylim[0])
    zr = abs(zlim[1] - zlim[0])

    max_range = max(xr, yr, zr)
    if max_range < 1e-12:
        max_range = 1.0

    xmid = np.mean(xlim)
    ymid = np.mean(ylim)
    zmid = np.mean(zlim)

    ax.set_xlim3d([xmid - max_range/2, xmid + max_range/2])
    ax.set_ylim3d([ymid - max_range/2, ymid + max_range/2])
    ax.set_zlim3d([zmid - max_range/2, zmid + max_range/2])

# ============================================================
# PLOT 2D con ellissi di confidenza
# ============================================================

def plot_trajectory_xy_with_ellipses(opt_poses: List[Tuple[float, np.ndarray, np.ndarray]],
                                     cov_all: List[Dict],
                                     gt_xyz: Optional[np.ndarray] = None,
                                     n_ellipses: int = 30,
                                     sigma_scale: float = 3.0,
                                     title: str = "Trajectory (x-y) with uncertainty",
                                     show: bool = True):
    """
    Plot 2D (x-y) con ellissi di confidenza ogni ~n_ellipses pose.
    
    Args:
        opt_poses: [(t, p(3,), q(4,)), ...]
        cov_all: [{time, k_idx, pose_cov(6x6), ...}, ...]
        gt_xyz: (M, 3) ground truth (opzionale)
        n_ellipses: numero di ellissi da plottare
        sigma_scale: moltiplicatore sigma (3.0 = 99.7% confidenza)
    """
    if not opt_poses or not cov_all:
        print("No data to plot.")
        return

    slam_xyz = np.array([p for (_, p, _, _) in opt_poses], dtype=np.float64)
    slam_times = np.array([t for (t, _, _, _) in opt_poses])

    # Mappa time -> cov (usa la cov della key più vicina in tempo)
    time_to_cov = {}
    for entry in cov_all:
        t_entry = entry["time"]
        pose_cov = entry["pose_cov"]  # 6x6
        time_to_cov[t_entry] = pose_cov

    fig, ax = plt.subplots(figsize=(10, 10))

    # Ground truth
    if gt_xyz is not None:
        ax.plot(gt_xyz[:, 0], gt_xyz[:, 1], 'k--', linewidth=3, alpha=0.6, label="Ground Truth")

    # SLAM trajectory
    ax.plot(slam_xyz[:, 0], slam_xyz[:, 1], 'b-', linewidth=3, label="Trajectory Estimate")
    ax.scatter(slam_xyz[0, 0], slam_xyz[0, 1], c='g', s=80, marker='o', label="Start", zorder=5)
    ax.scatter(slam_xyz[-1, 0], slam_xyz[-1, 1], c='r', s=80, marker='x', label="End", zorder=5)

    # Ellissi di confidenza (ogni ~n_ellipses pose)
    step = max(1, len(opt_poses) // n_ellipses)
    for i in range(0, len(opt_poses), step):
        t_i = slam_times[i]
        # Trova cov più vicina
        closest_t = min(time_to_cov.keys(), key=lambda t: abs(t - t_i))
        pose_cov = time_to_cov[closest_t]

        # Estrai sottomatrice posizione x-y (righe/colonne 3,4 in [rot(3) | pos(3)])
        cov_xy = pose_cov[3:5, 3:5]  # 2x2

        # Eigenvalues/eigenvectors per ellisse
        eigvals, eigvecs = np.linalg.eig(cov_xy)
        angle = np.degrees(np.arctan2(eigvecs[1, 0], eigvecs[0, 0]))
        width = 2 * sigma_scale * np.sqrt(eigvals[0])
        height = 2 * sigma_scale * np.sqrt(eigvals[1])

        ellipse = Ellipse(xy=(slam_xyz[i, 0], slam_xyz[i, 1]),
                          width=width, height=height, angle=angle,
                          edgecolor='orange', facecolor='orange', alpha=0.2, linewidth=1.5)
        # ax.add_patch(ellipse)

    ax.set_xlabel(r"$x [m]$", fontsize=20)
    ax.set_ylabel(r"$y [m]$", fontsize=20)
    # ax.set_title(title)
    ax.grid(True)
    ax.legend(loc='lower right', fontsize=20)
    ax.tick_params(axis='both', labelsize=20)
    plt.tight_layout()
    if show:
        plt.show()
    return fig, ax


# ============================================================
# PLOT 3D con tube di confidenza (raggio variabile)
# ============================================================

def plot_trajectory_3d_with_tube(opt_poses: List[Tuple[float, np.ndarray, np.ndarray]],
                                 cov_all: List[Dict],
                                 gt_xyz: Optional[np.ndarray] = None,
                                 sigma_scale: float = 3.0,
                                 tube_alpha: float = 0.15,
                                 n_segments: int = 50,
                                 title: str = "Trajectory 3D with uncertainty tube",
                                 show: bool = True):
    """
    Plot 3D con tube di confidenza (raggio variabile basato su σ posizionale).
    
    Args:
        opt_poses: [(t, p(3,), q(4,)), ...]
        cov_all: [{time, k_idx, pose_cov(6x6), ...}, ...]
        gt_xyz: (M, 3) ground truth (opzionale)
        sigma_scale: moltiplicatore sigma
        tube_alpha: trasparenza tube
        n_segments: numero di segmenti per tube
    """
    if not opt_poses or not cov_all:
        print("No data to plot.")
        return

    slam_xyz = np.array([p for (_, p, _, _) in opt_poses], dtype=np.float64)
    slam_times = np.array([t for (t, _, _, _) in opt_poses])

    # Mappa time -> sigma posizionale
    time_to_sigma = {}
    for entry in cov_all:
        t_entry = entry["time"]
        pose_cov = entry["pose_cov"]  # 6x6
        # Sigma posizione: sqrt(diag[3:6])
        sigma_pos = np.sqrt(np.diag(pose_cov)[3:6])  # [σ_x, σ_y, σ_z]
        sigma_norm = np.linalg.norm(sigma_pos)
        time_to_sigma[t_entry] = sigma_norm

    fig = plt.figure(figsize=(12, 9))
    ax = fig.add_subplot(111, projection='3d', facecolor='white')

    # Ground truth
    if gt_xyz is not None:
        ax.plot(gt_xyz[:, 0], gt_xyz[:, 1], gt_xyz[:, 2], 'k--', linewidth=3, alpha=0.6, label="Ground Truth")

    # SLAM trajectory
    ax.plot(slam_xyz[:, 0], slam_xyz[:, 1], slam_xyz[:, 2], 'b-', linewidth=3, label="Trajectory Estimate")
    ax.scatter(slam_xyz[0, 0], slam_xyz[0, 1], slam_xyz[0, 2], c='g', s=100, marker='o', label="Start")
    ax.scatter(slam_xyz[-1, 0], slam_xyz[-1, 1], slam_xyz[-1, 2], c='r', s=100, marker='x', label="End")

    # Tube di confidenza: cilindri tra pose consecutive con raggio variabile
    step = max(1, len(opt_poses) // n_segments)
    for i in range(0, len(opt_poses) - 1, step):
        t_i = slam_times[i]
        closest_t = min(time_to_sigma.keys(), key=lambda t: abs(t - t_i))
        radius = sigma_scale * time_to_sigma[closest_t]

        p1 = slam_xyz[i]
        p2 = slam_xyz[i + 1]

        # Disegna cilindro semplificato come linea spessa o sfera
        # (per semplicità, plottiamo sfere con raggio variabile)
        u = np.linspace(0, 2 * np.pi, 12)
        v = np.linspace(0, np.pi, 8)
        x_sphere = p1[0] + radius * np.outer(np.cos(u), np.sin(v))
        y_sphere = p1[1] + radius * np.outer(np.sin(u), np.sin(v))
        z_sphere = p1[2] + radius * np.outer(np.ones(np.size(u)), np.cos(v))

        # ax.plot_surface(x_sphere, y_sphere, z_sphere, color='orange', alpha=tube_alpha, linewidth=0)

    ax.set_xlabel(r"$x [m]$", fontsize=20)
    ax.set_ylabel(r"$y [m]$", fontsize=20)
    ax.set_zlabel(r"$z [m]$", fontsize=20)
    # ax.set_title(title, fontsize=20)
    ax.legend(loc="upper right", bbox_to_anchor=(1.0, 0.75), fontsize=15)
    ax.view_init(elev=17, azim=40, roll=0)
    ax.tick_params(axis='both', labelsize=20)
    _set_axes_equal_3d(ax)
    ax.set_zlim3d(-4, 10)
    plt.tight_layout()
    if show:
        plt.show()
    return fig, ax

# ============================================================
# Plot combinato 3D: traiettoria + landmarks (backward compatibility)
# ============================================================

def plot_trajectory_3d_on_ax(opt_poses,
                            ax,
                            label: str = "trajectory",
                            color: str = "tab:blue",
                            linewidth: float = 1.5):
    """
    opt_poses: [(t, p(3,), q(4,)), ...]
    Disegna solo la traiettoria su un ax 3D esistente.
    """
    if opt_poses is None or len(opt_poses) < 2:
        return

    xyz = np.array([p for (_, p, _, _) in opt_poses], dtype=np.float64)
    ax.plot(xyz[:, 0], xyz[:, 1], xyz[:, 2], color=color, linewidth=linewidth, label=label)

    # start/end
    ax.scatter(xyz[0, 0], xyz[0, 1], xyz[0, 2], color=color, s=40, marker="o")
    ax.scatter(xyz[-1, 0], xyz[-1, 1], xyz[-1, 2], color=color, s=40, marker="x")


def plot_features_3d_on_ax(opt_feats: dict,
                           ax,
                           opt_depths=None,
                           label: str = "landmarks",
                           color_by: str = "none",   # "none" | "rho" | "Z"
                           cmap: str = "viridis",
                           s: float = 6.0,
                           alpha: float = 0.9,
                           max_points: int | None = None):
    """
    opt_feats: fid -> {'X_world': (x,y,z), 'rho': ..., ...}
    opt_depths: opzionale lista [(fid, rho), ...] (fallback)
    """
    if opt_feats is None or len(opt_feats) == 0:
        return

    fids = list(opt_feats.keys())
    pts = np.array([opt_feats[fid]["X_world"] for fid in fids], dtype=np.float64)

    if max_points is not None and pts.shape[0] > int(max_points):
        rng = np.random.default_rng(0)
        idx = rng.choice(pts.shape[0], size=int(max_points), replace=False)
        pts = pts[idx]
        fids = [fids[i] for i in idx]

    cvals = None
    if color_by.lower() in ("rho", "z"):
        rho_map = {}
        for fid in fids:
            info = opt_feats.get(fid, {})
            if "rho" in info and np.isfinite(info["rho"]):
                rho_map[int(fid)] = float(info["rho"])

        if opt_depths is not None:
            for fid, rho in opt_depths:
                fid = int(fid)
                if fid not in rho_map and np.isfinite(rho):
                    rho_map[fid] = float(rho)

        rhos = np.array([rho_map.get(int(fid), np.nan) for fid in fids], dtype=np.float64)
        ok = np.isfinite(rhos) & (rhos > 0)
        pts = pts[ok]
        rhos = rhos[ok]

        if pts.shape[0] > 0:
            cvals = rhos if color_by.lower() == "rho" else (1.0 / rhos)

    if cvals is None:
        ax.scatter(pts[:, 0], pts[:, 1], pts[:, 2], s=s, alpha=alpha, label=label)
    else:
        sc = ax.scatter(pts[:, 0], pts[:, 1], pts[:, 2], c=cvals, s=s, alpha=alpha, cmap=cmap, label=label)
        cb = plt.colorbar(sc, ax=ax, pad=0.1, shrink=0.8)
        cb.set_label("rho [1/m]" if color_by.lower() == "rho" else "Z [m]")


def plot_vio_map_3d(opt_poses,
                     opt_feats,
                     opt_depths=None,
                     title: str = "VIO map (trajectory + landmarks)",
                     traj_color: str = "tab:blue",
                     feat_color_by: str = "none",
                     max_points: int | None = None,
                     show: bool = True):
    """
    Unico plot 3D con traiettoria + landmark (backward compatibility).
    """
    fig = plt.figure(figsize=(9, 7))
    ax = fig.add_subplot(111, projection="3d")

    plot_trajectory_3d_on_ax(opt_poses, ax=ax, label="trajectory", color=traj_color)
    plot_features_3d_on_ax(opt_feats, ax=ax, opt_depths=opt_depths, label="landmarks",
                           color_by=feat_color_by, max_points=max_points)

    ax.set_title(title)
    ax.set_xlabel("X [m]")
    ax.set_ylabel("Y [m]")
    ax.set_zlabel("Z [m]")
    ax.grid(True)
    ax.legend()

    _set_axes_equal_3d(ax)

    if show:
        plt.show()
    return ax