# EXPECT E-SCHEMA-SEALED
# spec §7.2.2 — `sector` is core-owned: residency and streaming are read and
# written by starcore itself, so a library redefining the form would leave
# the two disagreeing about the same bytes. Adding to it is still allowed,
# through `provides_schema` (§13.3).
schema = {
    id        = sector
    top_level = yes
    key = { name = id     type = identifier  required = yes }
    key = { name = radius type = int }
}
