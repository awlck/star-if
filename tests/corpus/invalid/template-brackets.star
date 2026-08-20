# EXPECT E-TEMPLATE-BRACKETS
# spec §9.1 — a template's interpolations run from '[' to ']', and a bracket
# meant literally is written \[. Both mistakes below are one character, and
# both are invisible until the message renders.
#
# The values are `text_or_script`-typed keys of the core-owned `action` form,
# which is what makes them templates: `[` and `]` are reserved for "the
# template language and parser grammar tokens" (§15), and the same file's
# `match` lines are the second of those. A checker that read every string as
# a template would report the grammar lines too.
action = {
    id    = pry
    match = { "pry [something]"  "pry open [something]" }

    successMsg = "You lever [the noun open."
    failureMsg = "It won't budge] no matter how hard you try."
}
