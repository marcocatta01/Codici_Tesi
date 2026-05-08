import numpy as np
import h5py

def read_events_h5_array(path: str,
                         time_div: float = 1e6,
                         chunk_rows: int = 2_000_000) -> np.ndarray:
    """
    Legge il dataset '/events' (N,4) da un file .h5 e restituisce un array (N,4) float64:
      colonne: [t_sec, x, y, pol]
    I timestamp nel file sono in microsecondi (us) e vengono convertiti in secondi con time_div=1e6.
    Il dataset '/events'ha shape (N,4)

    Parametri:
    - path: percorso al file .h5
    - time_div: divisore per convertire t in secondi (default 1e6)
    - chunk_rows: numero di righe per blocco nella lettura (per limitare i picchi di memoria)

    Si può valutare di inserire la lettura per chuncks dentro il loop di VIO.py per evitare di caricare tutto in memoria, 
    qui si assume che la RAM sia sufficiente a contenere tutti gli eventi.
    """

    with h5py.File(path, "r") as f:
        if "events" not in f:
            raise ValueError("Dataset '/events' non trovato nel file HDF5.")
        ds = f["events"]
        if not (ds.ndim == 2 and ds.shape[1] >= 4):
            raise ValueError(f"Shape del dataset '/events' non valida: atteso (N,4), trovato {ds.shape}")

        n = int(ds.shape[0])
        if n == 0:
            return np.empty((0, 4), dtype=np.float64)

        out = np.empty((n, 4), dtype=np.float64)
        for i0 in range(0, n, chunk_rows):
            i1 = min(i0 + chunk_rows, n)
            block = ds[i0:i1, 0:4]
            # Conversioni: t -> secondi, x/y/p copiati come float64
            out[i0:i1, 0] = block[:, 0] / time_div
            out[i0:i1, 1] = block[:, 1]
            out[i0:i1, 2] = block[:, 2]
            out[i0:i1, 3] = block[:, 3]
        return out