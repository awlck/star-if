# EXPECT E-BRACKET-OUTSIDE
# spec §3.7, §15 — '[' and ']' occur only inside string literals. They are
# reserved permanently for the template language and parser grammar tokens.
quest = {
    id = wrong
    stages = [ { id = one } { id = two } ]
}
