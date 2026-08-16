# EXPECT E-BLOCK-MIXED
# spec §7.6 — no declaration may be duplicated, and the rule is not limited to
# schemas: `schema-duplicate.star` covers two `schema` declarations, this the
# case an author actually hits, writing a second `take` when they meant
# `@replaces(star_core)`. Whichever loses, loses silently.
#
# The real code is E-SCHEMA-DUPLICATE, which needs the schema layer. Until
# that lands this pins the syntax and fails on a shape error, as the other
# schema-layer fixtures here do.
action = { id = take  match = { "take [something]" } }
action = { id = take  match = { "grab [something]" }  stray }
