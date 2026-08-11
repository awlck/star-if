# EXPECT E-BLOCK-MIXED
# spec §8.6.1 — a `side` block carries presentation, never state. Allowing
# `open` here is how the two halves of a door desynchronise, which is the bug
# the one-object design exists to prevent. Enforcing it needs the schema layer;
# this fixture pins the syntax and fails today on a shape error.
door = {
    id   = desyncing_hatch
    open = no
    side = { room = airlock  direction = out  open = yes  locked }
}
