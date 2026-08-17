# EXPECT E-TYPE-MISMATCH
# spec §6.6.1 — stdlib types `exits` as `map<direction, ref<room>>`, so the
# keys are values of the `direction` enum and `nrth` is not one. §14.3 names
# this case directly: "a map key in a path that is not a valid member of the
# key's type — `exits.nrth` for `map<direction, …>`".
#
# Without the check this is a door that silently is not there, discovered by
# a player walking into a wall.
room = {
    id    = your_cell
    exits = { nrth = corridor }
}
