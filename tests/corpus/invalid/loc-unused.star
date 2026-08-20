# EXPECT W-LOC-UNUSED
# spec §9.6 — inline strings are assigned generated keys at compile time, so
# a key written out by hand exists in order to be referenced. One that nothing
# references is a renamed reference or a string that moved, and either way the
# translator is being asked to translate a line no player will ever see.
loc = {
    lang = en

    door_creaks     = "The door creaks open."
    door_creeks     = "The door creeks open."
}

action = {
    id    = shove
    match = { "shove [something]" }

    successMsg = $door_creaks
}
