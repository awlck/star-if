# =============================================================================
#  stdlib — actions
# =============================================================================
#
#  `action` is a core-owned form (spec §7.2.4) because the turn sequence
#  dispatches on it. The actions themselves are not: every one below is this
#  library's, and a ruleset may add, replace or ignore them.
#
#  Inform 7 attaches special handling to the eighth action declared, expecting
#  it to be Going (§7.2.2). Nothing here depends on declaration order, and
#  nothing may: if core ever needs to know *which* action means something, it
#  has to be told by a marker rather than by counting.
# =============================================================================

action = {
    id    = take
    match = { "take [something]"  "get [something]"  "pick up [something]" }
    doc   = "Move an object into the actor's inventory."

    restrictions = {
        noun = { portable == yes  failureMsg = $take_not_portable }
    }
    effects = {
        move = { target = noun  holder = actor  relation = carried }
    }
    successMsg = $taken_default
}

action = {
    id    = drop
    match = { "drop [something]"  "put down [something]" }
    doc   = "Move a carried object into the actor's room."

    restrictions = {
        noun = { holder == actor  failureMsg = $drop_not_held }
    }
    effects = {
        move = { target = noun  holder = location  relation = in }
    }
    successMsg = $dropped_default
}

action = {
    id    = open
    match = { "open [something]" }
    doc   = "Open something openable."

    conditions   = { noun = { has_trait = openable } }
    restrictions = {
        noun = { open == no      failureMsg = $already_open }
        noun = { locked == no    failureMsg = $its_locked }
    }
    effects = {
        set = { target = noun  prop = open  value = yes }
    }
    successMsg = $opened_default
}

action = {
    id    = close
    match = { "close [something]"  "shut [something]" }
    doc   = "Close something openable."

    conditions   = { noun = { has_trait = openable } }
    restrictions = {
        noun = { open == yes  failureMsg = $already_closed }
    }
    effects = {
        set = { target = noun  prop = open  value = no }
    }
    successMsg = $closed_default
}
