# EXPECT E-BLOCK-MIXED
# spec §7.6 — `@replaces` names the library whose declaration is superseded,
# and it is an error if that library declared no such thing. That check is the
# whole point of naming a source: a typo, an upstream rename, or a library
# that stopped shipping the thing being patched all become build failures
# rather than a new declaration that never takes effect.
#
# The real code is E-SCHEMA-INVALID, which needs the schema layer.
action = @replaces(no_such_library) {
    id    = take
    match = { "take [something]" }
    stray
}
