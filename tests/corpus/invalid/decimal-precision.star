# EXPECT E-DEC-PRECISION
# EXPECT E-NUM-TRAILING-DOT
# spec §3.4 — decimals are fixed-point with exactly three fractional digits,
# and are rejected rather than rounded, because silently rounding a damage
# formula is the failure fixed-point exists to prevent.
weapon = {
    id     = imprecise
    weight = 1.5
    reach  = 2.
}
