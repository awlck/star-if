# EXPECT E-BLOCK-MIXED
# spec §6.6.1 — a map key in a path must be a valid member of the key's type.
# `exits` is map<direction, ref<room>>, so `nrth` is not merely absent, it is
# not a direction at all, and the compiler can say so. Detecting that needs the
# schema layer; this fixture pins the syntax and fails today on a shape error.
rule = {
    of_action  = go
    when       = { }
    conditions = { location = { exits.nrth == corridor  stray } }
}
