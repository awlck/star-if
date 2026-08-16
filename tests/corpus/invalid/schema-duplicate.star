# EXPECT E-SCHEMA-DUPLICATE
# spec §7.2 — two schemas may not declare the same form id. Unlike the sealed
# case this is not about ownership: whichever won, half the file would be
# validated against a shape its author did not write.
schema = {
    id        = loot_table
    top_level = yes
    key = { name = id  type = identifier  required = yes }
}

schema = {
    id        = loot_table
    top_level = yes
    key = { name = id     type = identifier  required = yes }
    key = { name = rolls  type = int }
}
