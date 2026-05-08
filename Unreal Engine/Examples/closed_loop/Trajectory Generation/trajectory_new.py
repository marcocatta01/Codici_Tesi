import os
import csv
import math
import numpy as np
import matplotlib.pyplot as plt

# ==========================
# Parametri simulazione
# ==========================
slow_factor = 5.0
dt = 0.1

OUTPUT_DIR = "./CSV_Files"
os.makedirs(OUTPUT_DIR, exist_ok=True)
OUTPUT_CSV = os.path.join(OUTPUT_DIR, "simulation.csv")

header = ["Time"]
header += [f"r_rov[{i}]" for i in range(1, 4)]
header += [f"q_rov[{i}]" for i in range(1, 5)]

# ==========================
# Utility
# ==========================
def quat_from_yaw(yaw):
    return np.array([0.0, 0.0, math.sin(yaw/2), math.cos(yaw/2)])

def angle_0_2pi(a):
    return a % (2 * math.pi)

def shortest_angle_diff(a, b):
    return (b - a + math.pi) % (2 * math.pi) - math.pi

# ==========================
# Trajectory builder
# ==========================
def trajectory():
    """
    Costruisce la traiettoria con due semicirconferenze:
    1. Prima semicirconferenza: da (0,0) a (0,10) passando per (-5,5), raggio 5m, v=0.25 m/s
    2. Seconda semicirconferenza: da (0,10) a (0,15) passando per (2.5,12.5), raggio 2.5m, v=0.25 m/s
    """
    steps = []
    
    # Parametri delle semicirconferenze
    v = 0.25  # velocità in norma (m/s)
    
    # Prima semicirconferenza (verso sinistra, senso orario)
    R1 = 5.0
    centro1 = np.array([0.0, 5.0])
    omega1 = v / R1  # velocità angolare (rad/s)
    T1 = math.pi / omega1  # durata (s)
    
    # Seconda semicirconferenza (verso destra, senso antiorario)
    R2 = 2.5
    centro2 = np.array([0.0, 12.5])
    omega2 = v / R2
    T2 = math.pi / omega2
    
    steps.append({
        'tipo': 'semi1',
        't_start': 0.0,
        't_end': T1,
        'R': R1,
        'omega': omega1,
        'centro': centro1
    })
    
    steps.append({
        'tipo': 'semi2',
        't_start': T1,
        't_end': T1 + T2,
        'R': R2,
        'omega': omega2,
        'centro': centro2
    })
    
    t_total = T1 + T2
    return steps, t_total

# ==========================
# Stato al tempo t
# ==========================
def stato(t, prepped):
    """
    Calcola posizione e orientamento al tempo t.
    """
    steps, t_end = prepped
    
    for step in steps:
        if t <= step['t_end']:
            tipo = step['tipo']
            R = step['R']
            omega = step['omega']
            centro = step['centro']
            t_rel = t - step['t_start']  # tempo relativo dall'inizio del tratto
            
            if tipo == 'semi1':
                # Prima semicirconferenza (senso orario, verso sinistra)
                # Parametrizzazione: alpha ∈ [0, π]
                alpha = omega * t_rel
                
                # Posizione: r(alpha) = centro + R*(-sin(alpha), 1-cos(alpha))
                x = centro[0] - R * math.sin(alpha)
                y = centro[1] - R * math.cos(alpha)
                
                # Heading tangenziale: heading = π - alpha
                # (derivata della posizione rispetto ad alpha)
                heading = math.pi - alpha
                yaw = angle_0_2pi(heading - math.pi / 2)
                
                r = np.array([x, y, 0.0])
                return r, quat_from_yaw(yaw), t_end
                
            elif tipo == 'semi2':
                # Seconda semicirconferenza (senso antiorario, verso destra)
                # Parametrizzazione: beta ∈ [0, π]
                beta = omega * t_rel
                
                # Posizione: r(beta) = centro + R*(sin(beta), -cos(beta))
                x = centro[0] + R * math.sin(beta)
                y = centro[1] - R * math.cos(beta)
                
                # Heading tangenziale: heading = beta
                heading = beta
                yaw = angle_0_2pi(heading - math.pi / 2)
                
                r = np.array([x, y, 0.0])
                return r, quat_from_yaw(yaw), t_end
    
    # Se siamo oltre l'ultimo step, ritorna la posizione finale
    last_step = steps[-1]
    r_final = np.array([0.0, 15.0, 0.0])
    yaw_final = angle_0_2pi(math.pi / 2 - math.pi / 2)  # heading finale = π/2 (verso +y)
    return r_final, quat_from_yaw(yaw_final), t_end

# ==========================
# CSV
# ==========================
steps, t_end_nominal = trajectory()
prepped = (steps, t_end_nominal)

print(f"Durata nominale traiettoria: {t_end_nominal:.2f} s")
print(f"Prima semicirconferenza: {steps[0]['t_end']:.2f} s")
print(f"Seconda semicirconferenza: {steps[1]['t_end'] - steps[1]['t_start']:.2f} s")

# t_static = 3.0 * slow_factor  # secondi di stazionarietà iniziale
t_static = 0.0  # secondi di stazionarietà iniziale
times = np.arange(t_static,
                  t_static + slow_factor*t_end_nominal + 1e-9, dt)

with open(OUTPUT_CSV, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(header)

    # Fase stazionaria iniziale (orientato verso -x, quindi yaw = π/2)
    for t in np.arange(0, t_static, dt):
        # Posizione iniziale (0,0,0), orientamento verso -x: yaw = π/2
        # quaternione: [0, 0, sin(π/4), cos(π/4)]
        w.writerow([f"{t:.6f}", 0, 0, 0, 0, 0, 
                   f"{math.sin(math.pi/4):.9f}", f"{math.cos(math.pi/4):.9f}"])

    # Fase dinamica
    for t in times:
        r, q, _ = stato((t-t_static)/slow_factor, prepped)
        w.writerow([f"{t:.6f}",
                    f"{r[0]:.6f}", f"{r[1]:.6f}", f"{r[2]:.6f}",
                    f"{q[0]:.9f}", f"{q[1]:.9f}",
                    f"{q[2]:.9f}", f"{q[3]:.9f}"])

print(f"CSV generato: {OUTPUT_CSV}")

# ==========================
# Plot
# ==========================
ts = np.arange(0, t_end_nominal, dt)
traj = np.array([stato(t, prepped)[0] for t in ts])

plt.figure(figsize=(8, 10))
plt.plot(traj[:,0], traj[:,1], 'b-', linewidth=2, label='Traiettoria ROV')
plt.plot(0, 0, 'go', markersize=10, label='Start (0,0)')
plt.plot(0, 10, 'ro', markersize=8, label='Giunzione (0,10)')
plt.plot(0, 15, 'ks', markersize=10, label='End (0,15)')
plt.plot(-5, 5, 'c^', markersize=8, label='Punto (-5,5)')
plt.plot(2.5, 12.5, 'm^', markersize=8, label='Punto (2.5,12.5)')

# Centri delle circonferenze
plt.plot(0, 5, 'rx', markersize=10, label='Centro 1')
plt.plot(0, 12.5, 'rx', markersize=10, label='Centro 2')

plt.axis("equal")
plt.grid(True, alpha=0.3)
plt.title("Traiettoria ROV - Due Semicirconferenze")
plt.xlabel("x [m]")
plt.ylabel("y [m]")
plt.legend()
plt.tight_layout()
plt.show()