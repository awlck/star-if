# EXPECT E-SCHEMA-DUPLICATE
# spec §7.6 — no declaration may be duplicated, and the rule is not limited to
# schemas: `schema-duplicate.star` covers two `schema` declarations, this the
# case an author actually hits, writing a second `take` when they meant
# `@replaces(stdlib)`. Whichever loses, loses silently.
action = { id = take  match = { "take [something]" } }
action = { id = take  match = { "grab [something]" } }
