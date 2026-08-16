# EXPECT E-DUP-KEY
# spec §5.3 — `successMsg` is declared `arity = one`, so a second binding of
# it is an error citing both spans. Without the check the second line simply
# wins, and an author who wrote two messages gets one with no indication that
# the other was discarded.
#
# `rule` is declared `arity = many` on `action` and repeats freely below,
# which is the other half of the same rule: whether a key may appear twice is
# the schema's to say, not the checker's.
action = {
    id         = polish_the_bell
    match      = { "polish bell" "buff bell" }
    successMsg = "You buff the bell until it gleams."
    successMsg = "The bell is already gleaming."

    rule = { id = polish_when_cracked  priority = 10 }
    rule = { id = polish_when_frozen   priority = 20 }
}
