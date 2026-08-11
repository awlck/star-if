# EXPECT E-BRACE-UNBALANCED
# spec §4 — the parser must report the block that was opened and never closed,
# not merely fail at end of file.
room = {
    id    = truncated
    exits = { north = corridor
}
