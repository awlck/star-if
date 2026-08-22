# EXPECT E-ANNOT-ARGUMENT
# spec §3.8 and §5.4.1. Both mistakes below are silent if unchecked, and
# neither is visible in play until much later.
#
# `@priority(n)` orders within a phase, "n is an integer, default 0". Written
# without one it orders nothing, and reads as though it does.
rule = {
    of_action  = go
    conditions = @priority { actor = { in_combat == yes } }
}

# `@platform` lists the frontends a statement is present on. A frontend
# nobody has heard of is not a narrower build — it is no build at all, so the
# rule below would be stripped everywhere while looking like a fallback for
# the text-only frontends.
rule = @platform(gtk) {
    of_action  = examine
    successMsg = @after "(no tooltip here)"
}

# Declared so that the only thing wrong with this file is the one thing it
# is about: a rule bound to an action nobody declares is its own error,
# and backlog F9 now reports it (§6.2, §14.3).
action = { id = go       match = { "go [direction]" } }
action = { id = examine  match = { "examine [something]" } }
