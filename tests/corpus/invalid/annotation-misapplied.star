# EXPECT E-ANNOT-MISAPPLIED
# spec §5.4.1 — each annotation has an "Applies to" column, and an annotation
# written where it cannot act is one an author has every reason to believe is
# working. `@merge` merges a block into an inherited block key by key; there
# is no key-by-key reading of a string.
rule = {
    of_action  = take
    successMsg = @merge "You take it, carefully."
}

# `@replaces(lib)` supersedes "a whole top-level declaration" (§7.6). On a key
# inside one it names a library that has no declaration of `match` to be
# superseded — combining a key's value with the inherited one is what
# `@override` and the rest are for.
action = {
    id    = polish_the_bell
    match = @replaces(stdlib) { "polish bell" }
}
