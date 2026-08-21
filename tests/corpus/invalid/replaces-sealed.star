# EXPECT E-SCHEMA-SEALED
# spec §7.2.2, §7.6 — sealing means extend freely, never supersede. Replacing
# the `class` form would change the shape of data the format layer parses with
# a reader of its own, which is the whole thing sealing exists to prevent.
#
# The owner named is `stardata`, because §7.2.4 makes `class` a format form.
# Like `starcore`, it is not a library id, so no `@replaces` can honestly
# claim it — naming it is what gets this past the wrong-source check and onto
# the sealing rule, which is the rule this fixture is about.
schema = @replaces(stardata) {
    id        = class
    top_level = yes
    key = { name = id  type = identifier  required = yes }
}
