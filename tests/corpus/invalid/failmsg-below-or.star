# EXPECT E-FAILMSG-UNREACHABLE
# spec §10.5.1 — an OR fails only when EVERY branch fails, so no single branch
# is the reason and none can explain it. The message belongs on the OR.
#
# The `conditions` stage narrows `actor` so that the strength check below is a
# property this slot is known to have (§8.8.3): narrowing flows forward from
# one stage to the next, and does not survive an OR branch, so the narrowing
# has to happen before the OR rather than inside it.
class_extension = { of_class = person  prop_def = { strength = int } }

action = {
    id    = shift_slab
    match = { "shift [something]" }

    conditions = { actor = { of_class = person } }
    restrictions = {
        OR = {
            actor    = { strength >= 14  failureMsg = "You aren't strong enough." }
            carrying = { holder = actor  obj = crowbar }
        }
    }
}
