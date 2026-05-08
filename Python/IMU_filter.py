import numpy as np
from scipy.signal import butter, sosfiltfilt

# Butterworth low-pass filter for IMU data (creato da funzioni già esistenti nelle librerie)

def imu_filter_pipeline(
    acc: np.ndarray,
    gyro: np.ndarray,
    fs: float = 100.0,
    fc_acc: float = 10.0,
    fc_gyro: float = 15.0,
    order: int = 6
):
    """
    Apply low-pass. Returns (acc_f, gyro_f).
    """
    wn_acc = fc_acc / (0.5 * fs)
    wn_gyro = fc_gyro / (0.5 * fs)

    sos_acc = butter(order, wn_acc, btype="low", output="sos")
    sos_gyro = butter(order, wn_gyro, btype="low", output="sos")

    acc_f = sosfiltfilt(sos_acc, acc, axis=0)
    gyro_f = sosfiltfilt(sos_gyro, gyro, axis=0)

    return acc_f, gyro_f

