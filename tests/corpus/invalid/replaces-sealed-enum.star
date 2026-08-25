# EXPECT E-SCHEMA-SEALED
# spec §7.2.2, §7.6 — an enum is sealed exactly like a schema, class or trait
# can be, and sealing means the same thing everywhere: extend freely, never
# supersede. `advances_turn_enum` (libs/starcore/builtin/schema.star) is
# sealed because its three values are not vocabulary a ruleset might
# reasonably extend, they are the whole of what the turn sequencer knows how
# to do with a round — unlike `relation_enum`, which is deliberately left
# unsealed for exactly this mechanism (see the comment above it in
# libs/starcore/builtin/object.star, and the test at the bottom of
# tests/unit/starcore/placement_test.cpp).
enum = @replaces(starcore) {
    id     = advances_turn_enum
    values = { on_success always never sometimes }
}
