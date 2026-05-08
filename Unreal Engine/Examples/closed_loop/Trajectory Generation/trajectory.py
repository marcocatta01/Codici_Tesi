import os
import csv
import math
import numpy as np
import matplotlib.pyplot as plt

# ==========================
# Parametri simulazione
# ==========================
slow_factor = 5.0      # Fattore di rallentamento per il CSV
dt = 0.1               # Passo temporale

# Output CSV
OUTPUT_DIR = "./CSV_Files"
os.makedirs(OUTPUT_DIR, exist_ok=True)
OUTPUT_CSV = os.path.join(OUTPUT_DIR, "simulation.csv")

# Header CSV
header = ["Time"]
header += [f"r_rov[{i}]" for i in range(1, 4)]
header += [f"q_rov[{i}]" for i in range(1, 5)]

# ==========================
# Utility angoli / quaternion
# ==========================
def quat_from_yaw(yaw):
    """Quaternion (x,y,z,w) per rotazione intorno a z (yaw)."""
    s = math.sin(yaw / 2.0)
    c = math.cos(yaw / 2.0)
    return np.array([0.0, 0.0, s, c])

def angle_0_2pi(a):
    """Normalizza in [0, 2π)."""
    return a % (2.0 * math.pi)

def shortest_angle_diff(a, b):
    """
    Ritorna l'angolo minimo (b - a) normalizzato in (-π, π].
    Positivo => rotazione "antioraria" (incremento yaw).
    Negativo => rotazione "oraria".
    """
    return (b - a + math.pi) % (2.0 * math.pi) - math.pi

# ==========================
# Definizione waypoints (x, y, z)
# ==========================
waypoints = [
    np.array([0.0, 0.0, 0.0]),
    np.array([0.0, 10.0, 0.0]),
    np.array([8.0, 18.0, 0.0]),
    np.array([-8.0, 34.0, 0.0]),
    np.array([0.0, 44.0, 0.0]),
    np.array([-15.0, 40.0, 0.0]),
    np.array([0.0, 30.0, 0.0]),
    np.array([5.0, 20.0, 0.0]),
    np.array([0.0, 0.0, 0.0]),
]

# ==========================
# Costruzione steps della traiettoria
# ==========================
def trajectory():
    """
    Costruisce la lista di steps:
      - eventuale rotazione iniziale verso yaw del primo segmento lineare
      - sequenza: (linear) + (rotate verso prossimo segmento) ... fino all'ultimo linear
      - nessuna rotazione finale dopo l'ultimo tratto lineare.
    """
    V_CASE3 = 0.5      # Velocità lineare costante sui tratti
    om_rotation = math.pi / 12  # Velocità angolare (rad/s)
    steps = []
    t_cursor = 0.0

    n_wp = len(waypoints)
    n_segments = n_wp - 1
    if n_segments <= 0:
        return waypoints, steps, t_cursor

    # Calcolo yaw per ciascun segmento lineare
    # Convenzione: yaw_line = heading - π/2 (come nel tuo script originale)
    yaw_line = []
    for i in range(n_segments):
        p0 = waypoints[i]
        p1 = waypoints[i + 1]
        d = p1[:2] - p0[:2]
        L = float(np.linalg.norm(d))
        if L > 1e-12:
            heading = math.atan2(d[1], d[0])
        else:
            heading = 0.0
        yaw = angle_0_2pi(heading - math.pi / 2.0)
        yaw_line.append(yaw)

    # Rotazione iniziale dalla yaw = 0 (identità) verso yaw_line[0]
    yaw_initial = 0.0
    yaw_target_first = yaw_line[0]
    diff0 = shortest_angle_diff(yaw_initial, yaw_target_first)
    delta0 = abs(diff0)
    if delta0 > 1e-9:  # Se serve ruotare
        rot_dir = 1 if diff0 >= 0 else -1
        rot_duration = delta0 / om_rotation if om_rotation != 0 else 0.0
        steps.append({
            "type": "rotate",
            "t0": t_cursor,
            "t1": t_cursor + rot_duration,
            "p": waypoints[0].copy(),
            "yaw_start": yaw_initial,
            "yaw_end": yaw_target_first,
            "delta": delta0,
            "rot_dir": rot_dir
        })
        t_cursor += rot_duration

    # Costruzione dei segmenti linear + rotazioni intermedie
    for i in range(n_segments):
        p0 = waypoints[i]
        p1 = waypoints[i + 1]
        d = p1[:2] - p0[:2]
        L = float(np.linalg.norm(d))
        T_lin = L / V_CASE3 if L > 0 else 0.0

        # Step lineare
        steps.append({
            "type": "linear",
            "t0": t_cursor,
            "t1": t_cursor + T_lin,
            "p0": p0.copy(),
            "p1": p1.copy(),
            "yaw": yaw_line[i]
        })
        t_cursor += T_lin

        # Rotazione verso prossima yaw (se non è l'ultimo segmento)
        if i < n_segments - 1:
            yaw0 = yaw_line[i]
            yaw1 = yaw_line[i + 1]
            diff = shortest_angle_diff(yaw0, yaw1)
            delta = abs(diff)
            if delta > 1e-9:
                rot_dir = 1 if diff >= 0 else -1
                rot_duration = delta / om_rotation if om_rotation != 0 else 0.0
                steps.append({
                    "type": "rotate",
                    "t0": t_cursor,
                    "t1": t_cursor + rot_duration,
                    "p": waypoints[i + 1].copy(),
                    "yaw_start": yaw0,
                    "yaw_end": yaw1,
                    "delta": delta,
                    "rot_dir": rot_dir
                })
                t_cursor += rot_duration
            # Se delta ~ 0: nessuna rotazione aggiunta

    # Nessuna rotazione finale dopo l'ultimo linear (come richiesto)
    return waypoints, steps, t_cursor

# ==========================
# Stato al tempo t
# ==========================
def stato(t, prepped):
    steps, t_end = prepped

    if not steps:
        return np.array([0.0, 0.0, 0.0]), quat_from_yaw(0.0), t_end

    if t >= t_end:
        step_idx = len(steps) - 1
    else:
        step_idx = next((i for i, s in enumerate(steps)
                         if s["t0"] <= t <= s["t1"]), len(steps) - 1)

    s = steps[step_idx]

    if s["type"] == "linear":
        T = s["t1"] - s["t0"]
        alpha = (t - s["t0"]) / T if T > 0 else 1.0
        alpha = max(0.0, min(1.0, alpha))
        p = s["p0"] * (1 - alpha) + s["p1"] * alpha
        r_rov = p.copy()
        yaw = s["yaw"]
    else:  # rotate
        T = s["t1"] - s["t0"]
        frac = (t - s["t0"]) / T if T > 0 else 1.0
        frac = max(0.0, min(1.0, frac))
        yaw_start = s["yaw_start"]
        delta = s["delta"]
        rot_dir = s["rot_dir"]
        yaw = angle_0_2pi(yaw_start + rot_dir * frac * delta)
        r_rov = s["p"].copy()

    q_rov = quat_from_yaw(yaw)
    return r_rov, q_rov, t_end

# ==========================
# Generazione CSV (rallentato)
# ==========================
waypoints, steps, t_end_nominal = trajectory()
prepped = (steps, t_end_nominal)

# Generazione traiettoria sul posto
t_start = 0.0
t_movement = 15.0  # Inizio movimento dopo 15 secondi (con slow_factor = 5 => 3 secondi nominali)
times_static = np.arange(t_start, t_movement, dt)
times = np.arange(t_movement, t_end_nominal * slow_factor + 1e-9, dt)

with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(header)

    # ==========================
    # Fase statica
    # ==========================
    for t in times_static:
        row = [f"{t:.6f}"]
        row += [f"{v:.6f}" for v in np.zeros(3)]   # posizione = 0
        row += [f"{v:.9f}" for v in [0.0, 0.0, 0.0, 1.0]]  # quaternione identità
        w.writerow(row)

    # ==========================
    # Fase dinamica
    # ==========================
    for t in times:
        t_model = (t - t_movement) / slow_factor
        r_rov, q_rov, _ = stato(t_model, prepped)

        row = [f"{t:.6f}"]
        row += [f"{v:.6f}" for v in r_rov]
        row += [f"{v:.9f}" for v in q_rov]
        w.writerow(row)

print(
    f"Generato '{OUTPUT_CSV}' con dt={dt} s. "
    f"Rallentamento slow_factor={slow_factor} (durata x{slow_factor}, velocità /{slow_factor})."
)

# ==========================
# Simulazione + Plot (tempo nominale)
# ==========================
waypoints_plot, steps_plot, t_end_plot = trajectory()
prepped_plot = (steps_plot, t_end_plot)

times_plot = np.arange(0.0, t_end_plot + 1e-9, dt)
traj_plot = np.array([stato(t, prepped_plot)[0] for t in times_plot])

fig, ax = plt.subplots(figsize=(7, 7))
ax.plot(traj_plot[:, 0], traj_plot[:, 1], 'k', label="Path")

# Waypoints
wp_xy = np.array([w[:2] for w in waypoints_plot])
ax.scatter(wp_xy[:, 0], wp_xy[:, 1], c='k', marker='o', s=40, zorder=6, label="Waypoints")
for i, w in enumerate(waypoints_plot):
    if i < len(waypoints_plot):
        ax.annotate(f"WP{i}", (w[0] + 0.4, w[1] + 0.4), fontsize=9, zorder=7)

# Frecce direzione tratti lineari
for s in steps_plot:
    if s["type"] == "linear":
        p0, p1 = s["p0"], s["p1"]
        mid = 0.5 * (p0 + p1)
        dir_vec = p1 - p0
        norm2 = np.linalg.norm(dir_vec[:2])
        if norm2 < 1e-9:
            continue
        dir_unit = dir_vec / norm2
        ax.arrow(mid[0], mid[1], dir_unit[0] * 1.5, dir_unit[1] * 1.5,
                 head_width=1.0, head_length=1.5, fc='r', ec='r',
                 length_includes_head=True, zorder=3)

ax.set_aspect('equal', 'box')
ax.set_xlabel("X [m]")
ax.set_ylabel("Y [m]")
ax.legend()
ax.grid(True)

# Padding assi
ax.relim()
ax.autoscale_view()
x0, x1 = ax.get_xlim()
y0, y1 = ax.get_ylim()
pad_x = 0.05 * (x1 - x0)
pad_y = 0.05 * (y1 - y0)
ax.set_xlim(x0 - pad_x, x1 + pad_x)
ax.set_ylim(y0 - pad_y, y1 + pad_y)

plt.show()