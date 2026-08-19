# EXPECT E-PROPDEF-TYPE-MISMATCH
# EXPECT W-PROPDEF-REDUNDANT
# spec §8.7 — "a local `prop_def` MUST NOT redeclare a name the object already
# inherits, with a different type. Redeclaring with the *same* type is
# redundant and SHOULD be reported as such."
#
# The two halves fail differently. A different type is a correctness failure:
# everything that reads a `console` reads `times_rebooted` at the width the
# class declared, and the save format is one of those readers. The redundant
# case is only noise — but it is the noise a property leaves behind when it is
# promoted to its class, and nothing else would ever mention it.
class = {
    id       = console
    of_class = thing
    prop_def = { times_rebooted = int }
}

console = {
    id       = odd_console
    prop_def = { times_rebooted = string }
}

console = {
    id       = spare_console
    prop_def = { times_rebooted = int }
}

# The ordinary case, and the one §8.7 exists for: a property belonging to
# exactly one object, declared in one line rather than by inventing a class
# with a single instance. No diagnostic — this is what the rule above is
# protecting, not something it refuses.
console = {
    id       = diagnostic_console
    prop_def = { diagnostic_code = string }
    diagnostic_code = "E-114"
}
