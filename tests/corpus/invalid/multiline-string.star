# EXPECT E-STR-MULTILINE
# spec §3.5 — a string literal may not span a line terminator.
# The legal way to write this is adjacent-literal concatenation (§3.5.1).
room = {
    id = broken
    description = "This literal opens here
                   and closes on the next line, which is not allowed."
}
