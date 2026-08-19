# EXPECT E-PROP-ABSENT
# spec §8.8.2 — "nothing in the program that could satisfy `T` declares `P`.
# **Error.** This is the case that catches typos, and it is the common one."
#
# `shineyness` is declared by nothing, anywhere. Left unchecked it would be a
# condition that raises the first time a player polishes something, in a
# build that compiled cleanly — which is the class of failure §1.3 exists to
# move out of play and into the build.
action = {
    id    = polish
    match = { "polish [something]" }
    restrictions = {
        noun = { of_class = thing
                 shineyness > 3
                 failureMsg = "It is already gleaming." }
    }
}
