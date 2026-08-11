# EXPECT E-FAILMSG-UNREACHABLE
# spec §10.5.1 — a NOT fails when its contents SUCCEED, so the NOT is what
# failed. A message on the inner block describes the opposite of what happened
# and would never be printed. It belongs on the NOT itself.
action = {
    id = take_wrong
    restrictions = {
        NOT = {
            carrying = { holder = actor
                         obj    = noun
                         failureMsg = "You are already holding it." }
        }
    }
}
