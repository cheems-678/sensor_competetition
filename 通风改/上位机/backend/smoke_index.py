"""Existing PA7 relative-index definition, matching frontend smoke-index.ts."""
import math

REFERENCE_POINTS = ((350, 0), (850, 25), (1500, 60), (2500, 100))


def smoke_index(pa7_mv):
    if type(pa7_mv) not in (int, float) or not math.isfinite(pa7_mv) or not 0 <= pa7_mv <= 3600:
        return None
    if pa7_mv <= REFERENCE_POINTS[0][0]:
        return 0
    for (low_mv, low_index), (high_mv, high_index) in zip(REFERENCE_POINTS, REFERENCE_POINTS[1:]):
        if pa7_mv <= high_mv:
            # JS Math.round for nonnegative values; Python round uses a different tie rule.
            return math.floor(low_index + (pa7_mv - low_mv) * (high_index - low_index) / (high_mv - low_mv) + .5)
    return 100
