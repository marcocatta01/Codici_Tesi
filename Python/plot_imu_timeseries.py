import numpy as np
import matplotlib.pyplot as plt

# Codice per confrontare i dati IMU raw e filtrati, con etichette in LaTeX e grafici ben formattati.

def plot_imu_timeseries(
    imu_data_raw: np.ndarray,
    acc_filt: np.ndarray,
    gyro_filt: np.ndarray
):
    """
    Plot IMU time series: both raw and filtered data.
    
    Arguments:
        imu_data_raw:   (N,7) array [timestamp, ax, ay, az, gx, gy, gz]
        acc_filt:       (N,3) filtered accelerometer [ax, ay, az]
        gyro_filt:      (N,3) filtered gyroscope [gx, gy, gz]
    """
    t = imu_data_raw[:, 0]
    acc_raw = imu_data_raw[:, 1:4]
    gyro_raw = imu_data_raw[:, 4:7]

    # LaTeX labels
    acc_labels = [r"$a_x$", r"$a_y$", r"$a_z$"]
    gyro_labels = [r"$\omega_x$", r"$\omega_y$", r"$\omega_z$"]
    
    fig, axs = plt.subplots(6, 1, figsize=(10, 14), sharex=True)
    
    # Accelerometer
    for i in range(3):
        axs[i].plot(t, acc_raw[:, i], 'b-', label=acc_labels[i])
        axs[i].plot(t, acc_filt[:, i], 'r-', label=acc_labels[i] + " filtered", alpha=0.8)
        axs[i].set_ylabel(acc_labels[i], fontsize=20)
        axs[i].legend(loc='upper right', fontsize=17, frameon=True)
        axs[i].grid(True, linestyle='--', alpha=0.5)
        axs[i].tick_params(axis='both', labelsize=15)
    
    # Gyroscope
    for i in range(3):
        axs[i+3].plot(t, gyro_raw[:, i], 'b-', label=gyro_labels[i])
        axs[i+3].plot(t, gyro_filt[:, i], 'r-', label=gyro_labels[i] + " filtered", alpha=0.8)
        axs[i+3].set_ylabel(gyro_labels[i], fontsize=20)
        axs[i+3].legend(loc='upper right', fontsize=17, frameon=True)
        axs[i+3].grid(True, linestyle='--', alpha=0.5)
        axs[i+3].tick_params(axis='both', labelsize=15)
    
    axs[-1].set_xlabel(r"Time $[s]$", fontsize=20)
    plt.tight_layout(rect=[0, 0, 1, 0.97])
    plt.show()