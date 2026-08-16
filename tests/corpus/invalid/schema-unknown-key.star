# EXPECT E-UNKNOWN-KEY
# spec §7.3 — a form is closed unless it says otherwise, so a mistyped key is
# caught rather than quietly ignored. `alwyas_resident` is a typo for
# `always_resident`, and silently doing nothing is the failure mode this
# rule exists to prevent.
sector = {
    id              = station_alpha
    alwyas_resident = yes
}
