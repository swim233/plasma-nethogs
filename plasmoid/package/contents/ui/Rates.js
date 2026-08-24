.pragma library

const BINARY_UNITS = ["B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s"];
const DECIMAL_UNITS = ["B/s", "kB/s", "MB/s", "GB/s", "TB/s"];

/// Formats a byte rate the way a network monitor should: never more than three
/// significant digits, so the compact panel entry does not change width every
/// time the third decimal wobbles.
function format(bytesPerSecond, binary) {
    const base = binary ? 1024 : 1000;
    const units = binary ? BINARY_UNITS : DECIMAL_UNITS;

    let value = Math.max(0, bytesPerSecond || 0);
    let index = 0;
    while (value >= base && index < units.length - 1) {
        value /= base;
        index += 1;
    }

    let digits = 0;
    if (index > 0) {
        digits = value < 10 ? 1 : 0;
    }

    return value.toFixed(digits) + " " + units[index];
}

/// Largest value in the window, used to scale a sparkline. Returns 0 for an
/// empty or all-zero window so callers can skip drawing.
function peak(values) {
    let max = 0;
    for (let i = 0; i < values.length; ++i) {
        if (values[i] > max) {
            max = values[i];
        }
    }
    return max;
}
