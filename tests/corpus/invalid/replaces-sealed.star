# EXPECT E-SCHEMA-SEALED
# spec §7.2.2, §7.6 — sealing means extend freely, never supersede. Replacing
# the core `class` form would change the shape of data starcore reads directly,
# which is the whole thing sealing exists to prevent.
schema = @replaces(starcore) {
    id        = class
    top_level = yes
    key = { name = id  type = identifier  required = yes }
}
