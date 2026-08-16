# EXPECT E-TYPE-MISMATCH
# spec §6.2 — `advances_turn` is `enum<advances_turn_enum>`, whose values are
# `on_success`, `always` and `never`. An enum is a closed set precisely so
# that a misspelling is a compile error rather than an action that quietly
# never advances the turn.
action = {
    id            = check_inventory
    match         = { "inventory" "i" }
    advances_turn = sometimes
}
