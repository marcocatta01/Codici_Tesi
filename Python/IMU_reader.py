import numpy as np

# Nome colonne CSV
REQUIRED_COLUMNS = ('timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz')

def read_imu_csv_array(path: str, delimiter: str = ',') -> np.ndarray:
    """
    Legge un CSV IMU con header: timestamp,ax,ay,az,gx,gy,gz
    Restituisce un array NumPy di shape (N, 7) e dtype float64:
      col0=timestamp, col1=ax, col2=ay, col3=az, col4=gx, col5=gy, col6=gz
    - Ordina per timestamp
    - Righe con timestamp non valido (NaN/Inf) vengono scartate
    - Supporta commenti che iniziano con '#'
    """
    data = np.genfromtxt(
        path,
        delimiter=delimiter,
        names=True,         # prima riga è l'header
        comments="#",       # righe che iniziano con # sono ignorate
        dtype=np.float64,
        invalid_raise=False
    )

    if data.size == 0:
        return np.empty((0, 7), dtype=np.float64)

    names = data.dtype.names or ()
    missing = [c for c in REQUIRED_COLUMNS if c not in names]
    if missing:
        raise ValueError(f"Header mancante o colonne non trovate: {missing}. Trovate: {names}")

    t  = np.asarray(data['timestamp'], dtype=np.float64)
    ax = np.asarray(data['ax'], dtype=np.float64)
    ay = np.asarray(data['ay'], dtype=np.float64)
    az = np.asarray(data['az'], dtype=np.float64)
    gx = np.asarray(data['gx'], dtype=np.float64)
    gy = np.asarray(data['gy'], dtype=np.float64)
    gz = np.asarray(data['gz'], dtype=np.float64)

    # Filtra righe con timestamp non finito
    valid = np.isfinite(t)
    t  = t[valid]
    ax = ax[valid]; ay = ay[valid]; az = az[valid]
    gx = gx[valid]; gy = gy[valid]; gz = gz[valid]

    # Ordina per tempo
    order = np.argsort(t, kind="mergesort")

    # Concatena in un array (N, 7)
    arr = np.column_stack([
        t[order], ax[order], ay[order], az[order],
        gx[order], gy[order], gz[order]
    ])
    return arr