# EXPECT E-PROP-ABSENT
# EXPECT E-PROP-MAYBE-ABSENT
# spec §8.8.3's own worked example is `successMsg = "It is rated for
# [noun.damage] damage."`, and until backlog F12's message-template check
# nothing ever looked inside one for a read. A property reference in a
# message is exactly as much a fact about the object as one in a condition,
# and exactly as capable of being a typo.
class = {
    id       = weapon
    of_class = starcore.object
    prop_def = { damage = int }
}

# A typo, in a message rather than a condition.
action = {
    id         = inspect
    match      = { "inspect [something]" }
    successMsg = "It has a [noun.shineyness] look."
}

# `[something]` gives `noun` the root class, and nothing here narrows it
# first — some objects in scope declare `damage` and most do not, which is
# exactly prop-maybe-absent.star's situation with the read moved into a
# message.
action = {
    id         = appraise
    match      = { "appraise [something]" }
    successMsg = "Rated for [noun.damage] damage."
}

# The reason this feature exists: `when` narrows `noun` to `weapon` first,
# and §8.8.3 has that narrowing flow forward into every later stage —
# `successMsg` included. No diagnostic for this one.
rule = {
    of_action  = appraise
    when       = { noun = { of_class = weapon } }
    successMsg = "It is rated for [noun.damage] damage."
}
