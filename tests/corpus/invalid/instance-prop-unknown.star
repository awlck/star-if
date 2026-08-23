# EXPECT E-PROP-UNKNOWN
# spec §7.4 — "The keys permitted inside an instantiation block are those of
# the class's property set (§8), plus the universal keys `id`, `traits`,
# `in`, `on`, `part_of`, and `sector`." Backlog F11's own worked example is
# this one: "`times_reboofed` is an error rather than a second property"
# (§8.7). Left unchecked, a Clausewitz-style loader accepts the block, and
# `times_rebooted` quietly keeps its class default forever.
class = {
    id       = console
    of_class = thing
    prop_def = { times_rebooted = int }
}

console = {
    id             = bridge_console
    times_reboofed = 3
}

# The ordinary case, and the one the rule above is not supposed to touch: a
# universal key (`id`, `in`, `prop_def`) beside a real property, all correctly
# spelled. No diagnostic.
console = {
    id       = spare_console
    in       = bridge_console
    prop_def = { spare_of = ref<console> }
    spare_of = bridge_console
    times_rebooted = 1
}
