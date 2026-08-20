# EXPECT E-LOC-UNDEFINED
# EXPECT E-LOC-DUPLICATE
# EXPECT W-LOC-UNUSED
# spec §9.6 — a $key must resolve, and keys are unique within a language.
loc = {
    lang        = en
    room_name   = "Storage Bay"
    room_name   = "Storage Bay, again"
}

room = {
    id   = storage
    name = $room_name_that_does_not_exist
}
