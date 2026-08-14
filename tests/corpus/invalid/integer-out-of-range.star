# EXPECT E-INT-RANGE
# spec §3.4 — an integer is a signed 64-bit value, and an out-of-range
# literal is rejected rather than wrapped.
global = {
    id      = hoard_size
    type    = int
    initial = 99999999999999999999
}
