# EXPECT E-LOC-UNDEFINED
# EXPECT E-LOC-DUPLICATE
# spec §9.5 — a $key must resolve, and keys are unique within a language.
loc = {
    lang        = en
    room_name   = "Storage Bay"
    room_name   = "Storage Bay, again"
}

room = {
    id   = storage
    name = $room_name_that_does_not_exist
}
