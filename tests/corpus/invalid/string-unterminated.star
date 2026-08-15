# EXPECT E-STR-UNTERMINATED
# spec §3.5 — a literal that is never closed. Reaching this rather than
# E-STR-MULTILINE takes a quote that opens on the last line of the file, so
# this fixture deliberately ends without a trailing newline.
thing = {
    id          = brass_key
    description = "A small brass key, worn smooth.