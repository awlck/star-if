# EXPECT E-BLOCK-MIXED
# spec §6.6.1 — a bare identifier in an ARGUMENT position is always a global.
# It never resolves against the enclosing object scope, so this reference to
# `waypoints` inside `noun = { ... }` does not silently become a property of
# noun; it is an undeclared global. The fix is `collection = noun.waypoints`.
#
# Full resolution needs the schema layer; this fixture pins the syntax and
# fails today on a shape error.
thing = {
    id       = navcomp
    prop_def = { waypoints = list<identifier> }
}

rule = {
    of_action  = examine
    when       = { }
    conditions = {
        noun = { includes = { collection = waypoints  value = storage  extra } }
    }
}
