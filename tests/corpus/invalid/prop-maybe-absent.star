# EXPECT E-PROP-MAYBE-ABSENT
# spec §8.8.2 — "some objects satisfying `T` declare `P` and others do not.
# The author must resolve it."
#
# `[something]` matches one object in scope, so `noun` is statically an
# object and nothing more (§8.8.1). `open` belongs to the `openable` trait,
# which some things have and most do not — so this reads a property the noun
# may not have, and §8.8.3 requires the author to say which case they meant.
#
# The fix is a narrowing, not a cast: `has_trait = openable` earlier in the
# same conjunction both produces a real refusal for a non-openable noun and
# gives the compiler what it needs for the line after it.
action = {
    id    = peer_into
    match = { "peer into [something]" }
    restrictions = {
        noun = { open == yes
                 failureMsg = "It is closed." }
    }
}

# The same read, narrowed in an earlier STAGE rather than an earlier line.
# `when` gates `conditions`, which gates `restrictions`, so a narrowing in one
# flows forward into all of them (§8.8.3) — and the stage sequence comes from
# the schema's `stage_order`, not from the analysis. No diagnostic here.
rule = {
    of_action = peer_into
    when      = { noun = { has_trait = openable } }
    conditions = { noun = { open == yes } }
}
