# EXPECT E-FAILMSG-UNREACHABLE
# spec §10.5.1 — an OR fails only when EVERY branch fails, so no single branch
# is the reason and none can explain it. The message belongs on the OR.
action = {
    id = shift_slab
    restrictions = {
        OR = {
            actor    = { strength >= 14  failureMsg = "You aren't strong enough." }
            carrying = { holder = actor  obj = crowbar }
        }
    }
}
