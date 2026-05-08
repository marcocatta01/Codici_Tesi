import numpy as np

# Time Surface functions

def build_time_surface(sae_pos: np.ndarray, sae_neg: np.ndarray, t_now: float, tau: float):
    """
    Time Surface:
      TS = exp(-(t_now - t_last_pos)/tau) - exp(-(t_now - t_last_neg)/tau)
    Range ~ [-1,1]
    """
    dt_pos = np.clip(t_now - sae_pos, 0.0, None)
    dt_neg = np.clip(t_now - sae_neg, 0.0, None)
    ts_pos = np.exp(-dt_pos / tau)
    ts_neg = np.exp(-dt_neg / tau)
    TS = ts_pos - ts_neg
    return TS

def should_build_TS(events_since_last_TS: int, t_prev_TS: float, t_now: float, delta_t_max: float, events_threshold: int):
    """
    Condition to build a new Time Surface:
      - if more than delta_t_max seconds have passed since the last TS
      - if more than N events have arrived since the last TS
    """
    if (t_now - t_prev_TS) >= delta_t_max:
        return True
    if events_since_last_TS >= events_threshold:
        return True
    return False

def adaptive_tau(tau_base: float,
                 events_interval: int,
                 dt_interval: float,
                 ref_rate: float):
    """
    Adaptive tau: if event rate is low -> increase tau (slower decay),
    if event rate is high -> decrease tau.
    rate = events_interval / dt_interval
    scale = ref_rate / max(rate, eps)
    """
    eps = 1e-9
    rate = events_interval / max(dt_interval, eps)
    scale = ref_rate / max(rate, eps)
    tau_new = tau_base * np.sqrt(scale)
    return tau_new