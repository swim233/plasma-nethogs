.pragma library

/// Turns the configured animation speed into a duration in milliseconds.
///
/// `base` is normally one of the Kirigami.Units durations, so the system's own
/// animation speed setting remains the baseline and this only scales it.
/// Higher speed means shorter: 200 halves the duration, 50 doubles it.
///
/// Only the length is computed here. Switching animations off is done with the
/// `enabled` property of the Transition or Behavior, rather than a zero
/// duration, so nothing has to run at all.
function scaled(base, speedPercent) {
    return Math.max(1, Math.round(base * 100 / Math.max(25, speedPercent)));
}
