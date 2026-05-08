import numpy as np

# Questo file è interamente preso dalla repo GitHub dell'ArcStar Detector, 
# era in C++ e l'ho convertito in Python. Se ci sono dubbi o eventuali mancanze, 
# controllare il file originale in C++ dalla repo

class ArcStarDetector:
    """
    Asynchronous Arc* corner detector
    Maintains two SAEs (polarity 0/1) and corresponding 'latest' surfaces for spike filtering.
    NOTE:
      - Internally, matrices are [y,x].
    """
    def __init__(self, sensor_width: int = 640, sensor_height: int = 480, filter_threshold: float = 0.050):
        self.kSensorWidth = int(sensor_width)
        self.kSensorHeight = int(sensor_height)
        self.filter_threshold = float(filter_threshold)

        self.kSmallCircle = (
            (0, 3), (1, 3), (2, 2), (3, 1),
            (3, 0), (3, -1), (2, -2), (1, -3),
            (0, -3), (-1, -3), (-2, -2), (-3, -1),
            (-3, 0), (-3, 1), (-2, 2), (-1, 3)
        )
        self.kLargeCircle = (
            (0, 4), (1, 4), (2, 3), (3, 2),
            (4, 1), (4, 0), (4, -1), (3, -2),
            (2, -3), (1, -4), (0, -4), (-1, -4),
            (-2, -3), (-3, -2), (-4, -1), (-4, 0),
            (-4, 1), (-3, 2), (-2, 3), (-1, 4)
        )

        self.sae = (
            np.zeros((self.kSensorHeight, self.kSensorWidth), dtype=np.float64),
            np.zeros((self.kSensorHeight, self.kSensorWidth), dtype=np.float64),
        )
        self.sae_latest = (
            np.zeros((self.kSensorHeight, self.kSensorWidth), dtype=np.float64),
            np.zeros((self.kSensorHeight, self.kSensorWidth), dtype=np.float64),
        )

    def reset(self):
        for i in (0, 1):
            self.sae[i].fill(0.0)
            self.sae_latest[i].fill(0.0)

    def is_corner(self, et: float, ex: int, ey: int, ep: bool) -> bool:
        pol = 1 if ep else 0
        pol_inv = 0 if ep else 1

        t_last = self.sae_latest[pol][ey, ex]
        t_last_inv = self.sae_latest[pol_inv][ey, ex]

        # Spike filtering
        if (et > t_last + self.filter_threshold) or (t_last_inv > t_last):
            self.sae_latest[pol][ey, ex] = et
            self.sae[pol][ey, ex] = et
        else:
            self.sae_latest[pol][ey, ex] = et
            return False

        # Border check
        kBorderLimit = 4
        if (ex < kBorderLimit or ex >= (self.kSensorWidth - kBorderLimit) or
            ey < kBorderLimit or ey >= (self.kSensorHeight - kBorderLimit)):
            return False

        # Small & large circle parameters
        kSmallCircleSize = len(self.kSmallCircle)
        kLargeCircleSize = len(self.kLargeCircle)
        kSmallMinThresh = 3
        kSmallMaxThresh = 6
        kLargeMinThresh = 4
        kLargeMaxThresh = 8

        # --- Small circle ---
        is_arc_valid = False
        segment_new_min_t = self.sae[pol][ey + self.kSmallCircle[0][1], ex + self.kSmallCircle[0][0]]
        arc_right_idx = 0
        for i in range(1, kSmallCircleSize):
            dx, dy = self.kSmallCircle[i]
            t = self.sae[pol][ey + dy, ex + dx]
            if t > segment_new_min_t:
                segment_new_min_t = t
                arc_right_idx = i

        arc_left_idx = (arc_right_idx - 1 + kSmallCircleSize) % kSmallCircleSize
        arc_right_idx = (arc_right_idx + 1) % kSmallCircleSize

        dxL, dyL = self.kSmallCircle[arc_left_idx]
        dxR, dyR = self.kSmallCircle[arc_right_idx]
        arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
        arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
        arc_left_min_t = arc_left_value
        arc_right_min_t = arc_right_value

        iteration = 1
        while iteration < kSmallMinThresh:
            if arc_right_value > arc_left_value:
                if arc_right_min_t < segment_new_min_t:
                    segment_new_min_t = arc_right_min_t
                arc_right_idx = (arc_right_idx + 1) % kSmallCircleSize
                dxR, dyR = self.kSmallCircle[arc_right_idx]
                arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
                if arc_right_value < arc_right_min_t:
                    arc_right_min_t = arc_right_value
            else:
                if arc_left_min_t < segment_new_min_t:
                    segment_new_min_t = arc_left_min_t
                arc_left_idx = (arc_left_idx - 1 + kSmallCircleSize) % kSmallCircleSize
                dxL, dyL = self.kSmallCircle[arc_left_idx]
                arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
                if arc_left_value < arc_left_min_t:
                    arc_left_min_t = arc_left_value
            iteration += 1

        newest_segment_size = kSmallMinThresh
        while iteration < kSmallCircleSize:
            if arc_right_value > arc_left_value:
                if arc_right_value >= segment_new_min_t:
                    newest_segment_size = iteration + 1
                    if arc_right_min_t < segment_new_min_t:
                        segment_new_min_t = arc_right_min_t
                arc_right_idx = (arc_right_idx + 1) % kSmallCircleSize
                dxR, dyR = self.kSmallCircle[arc_right_idx]
                arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
                if arc_right_value < arc_right_min_t:
                    arc_right_min_t = arc_right_value
            else:
                if arc_left_value >= segment_new_min_t:
                    newest_segment_size = iteration + 1
                    if arc_left_min_t < segment_new_min_t:
                        segment_new_min_t = arc_left_min_t
                arc_left_idx = (arc_left_idx - 1 + kSmallCircleSize) % kSmallCircleSize
                dxL, dyL = self.kSmallCircle[arc_left_idx]
                arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
                if arc_left_value < arc_left_min_t:
                    arc_left_min_t = arc_left_value
            iteration += 1

        if ((newest_segment_size <= kSmallMaxThresh) or
            ((newest_segment_size >= (kSmallCircleSize - kSmallMaxThresh)) and
             (newest_segment_size <= (kSmallCircleSize - kSmallMinThresh)))):
            is_arc_valid = True

        # --- Large circle ---
        if is_arc_valid:
            is_arc_valid = False
            segment_new_min_t = self.sae[pol][ey + self.kLargeCircle[0][1], ex + self.kLargeCircle[0][0]]
            arc_right_idx = 0
            for i in range(1, kLargeCircleSize):
                dx, dy = self.kLargeCircle[i]
                t = self.sae[pol][ey + dy, ex + dx]
                if t > segment_new_min_t:
                    segment_new_min_t = t
                    arc_right_idx = i

            arc_left_idx = (arc_right_idx - 1 + kLargeCircleSize) % kLargeCircleSize
            arc_right_idx = (arc_right_idx + 1) % kLargeCircleSize

            dxL, dyL = self.kLargeCircle[arc_left_idx]
            dxR, dyR = self.kLargeCircle[arc_right_idx]
            arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
            arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
            arc_left_min_t = arc_left_value
            arc_right_min_t = arc_right_value

            iteration = 1
            while iteration < kLargeMinThresh:
                if arc_right_value > arc_left_value:
                    if arc_right_min_t < segment_new_min_t:
                        segment_new_min_t = arc_right_min_t
                    arc_right_idx = (arc_right_idx + 1) % kLargeCircleSize
                    dxR, dyR = self.kLargeCircle[arc_right_idx]
                    arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
                    if arc_right_value < arc_right_min_t:
                        arc_right_min_t = arc_right_value
                else:
                    if arc_left_min_t < segment_new_min_t:
                        segment_new_min_t = arc_left_min_t
                    arc_left_idx = (arc_left_idx - 1 + kLargeCircleSize) % kLargeCircleSize
                    dxL, dyL = self.kLargeCircle[arc_left_idx]
                    arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
                    if arc_left_value < arc_left_min_t:
                        arc_left_min_t = arc_left_value
                iteration += 1

            newest_segment_size = kLargeMinThresh
            while iteration < kLargeCircleSize:
                if arc_right_value > arc_left_value:
                    if arc_right_value >= segment_new_min_t:
                        newest_segment_size = iteration + 1
                        if arc_right_min_t < segment_new_min_t:
                            segment_new_min_t = arc_right_min_t
                    arc_right_idx = (arc_right_idx + 1) % kLargeCircleSize
                    dxR, dyR = self.kLargeCircle[arc_right_idx]
                    arc_right_value = self.sae[pol][ey + dyR, ex + dxR]
                    if arc_right_value < arc_right_min_t:
                        arc_right_min_t = arc_right_value
                else:
                    if arc_left_value >= segment_new_min_t:
                        newest_segment_size = iteration + 1
                        if arc_left_min_t < segment_new_min_t:
                            segment_new_min_t = arc_left_min_t
                    arc_left_idx = (arc_left_idx - 1 + kLargeCircleSize) % kLargeCircleSize
                    dxL, dyL = self.kLargeCircle[arc_left_idx]
                    arc_left_value = self.sae[pol][ey + dyL, ex + dxL]
                    if arc_left_value < arc_left_min_t:
                        arc_left_min_t = arc_left_value
                iteration += 1

            if ((newest_segment_size <= kLargeMaxThresh) or
                (newest_segment_size >= (kLargeCircleSize - kLargeMaxThresh) and
                 (newest_segment_size <= (kLargeCircleSize - kLargeMinThresh)))):
                is_arc_valid = True

        return is_arc_valid