# EXPECT E-BAD-CHAR
# spec §3 — a character that begins no token at all. `*=` additionally names
# an operator §15 reserves, so the diagnostic says so rather than treating
# the '*' as noise.
rule = {
    of_action = attack
    effects   = {
        hp *= 2
        note = ok?
    }
}
