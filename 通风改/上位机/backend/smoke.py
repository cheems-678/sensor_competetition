"""Operator-supplied PA7 reference; relative index only, not calibrated concentration."""
import math

REFERENCE_POINTS = ((350, 0), (850, 25), (1500, 60), (2500, 100))


def smoke_index(pa7_mv):
    if not isinstance(pa7_mv, (int, float)) or not math.isfinite(pa7_mv) or not 0 <= pa7_mv <= 3600:
        return None
    if pa7_mv <= 350:
        return 0
    for (low_mv, low_index), (high_mv, high_index) in zip(REFERENCE_POINTS, REFERENCE_POINTS[1:]):
        if pa7_mv <= high_mv:
            return math.floor(low_index + (pa7_mv - low_mv) * (high_index - low_index) / (high_mv - low_mv) + 0.5)
    return 100
