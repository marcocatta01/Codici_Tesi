import os
import csv
import math
import numpy as np
import matplotlib.pyplot as plt

# ==========================
# Parametri simulazione
# ==========================
slow_factor = 10.0
dt = 0.1
V_CASE3 = 0.25  # Velocità lineare costante sui tratti

OUTPUT_DIR = "C:\\Users\\marco\\Desktop"
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
    Costruisce la traiettoria completa:
    1. Tratto lineare: da (-171, -55) a (-171, -52)
    2. Primo arco (quarto di cerchio): da (-171, -52) a (-173, -50) con centro in (-171, -52)? No, il centro deve essere diverso
       Per un quarto di cerchio che va da (-171,-52) a (-173,-50), il centro potrebbe essere (-171,-50) o (-173,-52)
       Scegliamo centro in (-171,-50): raggio 2, angolo da -π/2 a 0
    3. Secondo arco (semicirconferenza): da (-173, -50) a (-178, -45) con centro in (-173, -45)
    """
    steps = []
    t_cursor = 0.0
    
    # ============================================
    # 1. Tratto lineare: da (-171, -55) a (-171, -52)
    # ============================================
    p0_lin = np.array([-175.0, -55.0, 0.0])
    p1_lin = np.array([-175.0, -52.0, 0.0])
    
    d_lin = p1_lin[:2] - p0_lin[:2]
    L_lin = float(np.linalg.norm(d_lin))
    T_lin = L_lin / V_CASE3
    
    # Calcolo yaw per il tratto lineare (movimento verso +y, quindi heading = π/2)
    heading_lin = math.atan2(d_lin[1], d_lin[0])  # π/2
    yaw_lin = heading_lin - math.pi / 2.0  # 0
    
    steps.append({
        'tipo': 'lineare',
        't_start': t_cursor,
        't_end': t_cursor + T_lin,
        'p0': p0_lin,
        'p1': p1_lin,
        'yaw': yaw_lin
    })
    t_cursor += T_lin
    
    # ============================================
    # 2. Primo arco (quarto di cerchio): da (-171, -52) a (-173, -50)
    #    Centro in (-171, -50)? Raggio 2, angolo da -π/2 a π? No
    #    Proviamo con centro in (-173, -52): 
    #    Punto iniziale (-171,-52): vettore da centro = (2, 0) → angolo = 0
    #    Punto finale (-173,-50): vettore da centro = (0, 2) → angolo = π/2
    #    Quindi arco di 90° in senso antiorario da 0 a π/2
    # ============================================
    centro_arco1 = np.array([-177.0, -52.0])
    R_arco1 = 2.0
    
    # Verifica: distanza da centro a (-171,-52) = 2, a (-173,-50) = 2
    omega_arco1 = V_CASE3 / R_arco1
    T_arco1 = (math.pi/2) / omega_arco1  # tempo per 90°
    
    steps.append({
        'tipo': 'arco1',
        't_start': t_cursor,
        't_end': t_cursor + T_arco1,
        'R': R_arco1,
        'omega': omega_arco1,
        'centro': centro_arco1,
        'angolo_inizio': 0.0,           # vettore (2,0) → angolo 0
        'angolo_fine': math.pi/2,        # vettore (0,2) → angolo π/2
        'verso': 'antiorario'
    })
    t_cursor += T_arco1
    
    # ============================================
    # 3. Secondo arco (semicirconferenza? No, quarto di cerchio): da (-173, -50) a (-178, -45)
    #    Centro in (-173, -45), raggio 5, angolo da π/2 a π? Verifichiamo:
    #    Punto (-173,-50): vettore da centro (-173,-45) = (0, -5) → angolo = -π/2 o 3π/2
    #    Punto (-178,-45): vettore da centro = (-5, 0) → angolo = π
    #    Quindi da 3π/2 a π in senso orario? O da -π/2 a π in senso antiorario?
    #    Da -π/2 a π è un arco di 270°, non va bene.
    #    In senso orario da 3π/2 a π è 90°: 3π/2 → π (diminuendo)
    # ============================================
    centro_arco2 = np.array([-177.0, -45.0])
    R_arco2 = 5.0
    
    omega_arco2 = V_CASE3 / R_arco2
    T_arco2 = (math.pi/2) / omega_arco2  # tempo per 90°
    
    steps.append({
        'tipo': 'arco2',
        't_start': t_cursor,
        't_end': t_cursor + T_arco2,
        'R': R_arco2,
        'omega': omega_arco2,
        'centro': centro_arco2,
        'angolo_inizio': 3*math.pi/2,     # vettore (0,-5) → angolo -π/2 = 3π/2
        'angolo_fine': math.pi,            # vettore (-5,0) → angolo π
        'verso': 'orario'                  # Senso orario (angolo diminuisce da 3π/2 a π)
    })
    t_cursor += T_arco2
    
    t_total = t_cursor
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
            t_rel = t - step['t_start']
            
            if tipo == 'lineare':
                # Tratto lineare
                p0 = step['p0']
                p1 = step['p1']
                yaw = step['yaw']
                
                # Interpolazione lineare
                T_tot = step['t_end'] - step['t_start']
                s = t_rel / T_tot
                
                r = p0 + s * (p1 - p0)
                return r, quat_from_yaw(yaw), t_end
                
            elif tipo in ['arco1', 'arco2']:
                # Arco di cerchio
                R = step['R']
                omega = step['omega']
                centro = step['centro']
                ang0 = step['angolo_inizio']
                verso = step['verso']
                
                if verso == 'orario':
                    # Senso orario: angolo diminuisce
                    ang = ang0 - omega * t_rel
                else:
                    # Senso antiorario: angolo aumenta
                    ang = ang0 + omega * t_rel
                
                # Posizione: centro + R*(cos(ang), sin(ang))
                x = centro[0] + R * math.cos(ang)
                y = centro[1] + R * math.sin(ang)
                
                # Heading tangenziale: 
                # - per cerchio antiorario: heading = ang + π/2
                # - per cerchio orario: heading = ang - π/2
                if verso == 'orario':
                    heading = ang - math.pi/2
                else:
                    heading = ang + math.pi/2
                    
                yaw = angle_0_2pi(heading - math.pi/2)
                
                r = np.array([x, y, 0.0])
                return r, quat_from_yaw(yaw), t_end
    
    # Se siamo oltre l'ultimo step, ritorna la posizione finale
    r_final = np.array([-182.0, -45.0, 0.0])
    yaw_final = angle_0_2pi(math.pi - math.pi/2)  # heading finale = π
    return r_final, quat_from_yaw(yaw_final), t_end

# ==========================
# CSV
# ==========================
steps, t_end_nominal = trajectory()
prepped = (steps, t_end_nominal)

print("=" * 50)
print("Dettaglio traiettoria:")
print("=" * 50)
print(f"Durata totale traiettoria: {t_end_nominal:.2f} s")
print("-" * 50)
t_cursor = 0
for i, step in enumerate(steps):
    print(f"Tratto {i+1}: {step['tipo']}")
    print(f"  da t = {t_cursor:.2f} s a t = {step['t_end']:.2f} s")
    print(f"  durata: {step['t_end'] - t_cursor:.2f} s")
    if step['tipo'] == 'lineare':
        print(f"  da ({step['p0'][0]:.1f},{step['p0'][1]:.1f}) a ({step['p1'][0]:.1f},{step['p1'][1]:.1f})")
        print(f"  yaw: {step['yaw']:.2f} rad")
    else:
        print(f"  raggio: {step['R']:.1f}m, centro: ({step['centro'][0]:.1f},{step['centro'][1]:.1f})")
        print(f"  verso: {step['verso']}")
        print(f"  angolo da {step['angolo_inizio']:.2f} a {step['angolo_fine']:.2f} rad")
    t_cursor = step['t_end']
print("=" * 50)

t_static = 0.0
times = np.arange(t_static,
                  t_static + slow_factor*t_end_nominal + 1e-9, dt)

with open(OUTPUT_CSV, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(header)

    # Fase stazionaria iniziale
    for t in np.arange(0, t_static, dt):
        w.writerow([f"{t:.6f}", -171, -55, 0, 0, 0, 
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
ts = np.arange(0, t_end_nominal, dt/10)  # Più punti per un plot più fluido
traj = np.array([stato(t, prepped)[0] for t in ts])

plt.figure(figsize=(12, 10))

# Traiettoria
plt.plot(traj[:,0], traj[:,1], 'b-', linewidth=2, label='Traiettoria ROV')

# Punti caratteristici
plt.plot(-171, -55, 'go', markersize=12, label='Start (-171,-55)')
plt.plot(-171, -52, 'yo', markersize=10, label='Fine linea/inizio arco1 (-171,-52)')
plt.plot(-173, -50, 'co', markersize=10, label='Fine arco1/inizio arco2 (-173,-50)')
plt.plot(-178, -45, 'ks', markersize=12, label='End (-178,-45)')

# Centri
plt.plot(-173, -52, 'rx', markersize=12, label='Centro arco1 (-173,-52)')
plt.plot(-173, -45, 'rx', markersize=12, label='Centro arco2 (-173,-45)')

# Punti intermedi per verifica
plt.plot(-171 + 2*math.cos(0), -52 + 2*math.sin(0), 'k.', markersize=1)
plt.plot(-173 + 5*math.cos(3*math.pi/2), -45 + 5*math.sin(3*math.pi/2), 'k.', markersize=1)

plt.axis("equal")
plt.grid(True, alpha=0.3)
plt.title("Traiettoria ROV - Linea + Due Archi di Cerchio")
plt.xlabel("x [m]")
plt.ylabel("y [m]")
plt.legend(loc='upper left', bbox_to_anchor=(1, 1))
plt.tight_layout()
plt.show()

print("\n" + "=" * 50)
print("Coordinate punti principali:")
print("=" * 50)
print(f"Start: (-171, -55)")
print(f"Fine linea/inizio arco1: (-171, -52)")
print(f"Fine arco1/inizio arco2: (-173, -50)")
print(f"Fine arco2: (-178, -45)")
print(f"Centro arco1: (-173, -52)")
print(f"Centro arco2: (-173, -45)")
print(f"\nCaratteristiche archi:")
print(f"Arco1: raggio 2m, 90° antiorario (da 0 a π/2)")
print(f"Arco2: raggio 5m, 90° orario (da 3π/2 a π)")