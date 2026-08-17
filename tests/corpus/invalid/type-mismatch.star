# EXPECT E-TYPE-MISMATCH
# spec §6.2 — `always_resident` is declared `bool`, which accepts `yes` and
# `no`. A number there is not a truthy value that happens to be spelled
# oddly; it is a key whose declared type says what it holds, and this is not
# one of those. Coercion would mean the engine reading a boolean out of a
# slot the author filled with something else.
sector = {
    id              = station_alpha
    always_resident = 3
}
