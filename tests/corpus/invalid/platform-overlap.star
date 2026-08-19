# EXPECT E-DUP-KEY
# spec §5.4.1 — `@platform` alternatives for one `arity = one` key are a
# single binding with a run-time selector, PROVIDED their frontends are
# disjoint. These two share `cli`, so on the text-only frontend the engine
# would hold two messages for one action and no rule to choose between them.
#
# The failure this prevents is a quiet one: `qt` and `web` get the first
# message, `glk` gets the second, and `cli` gets whichever the loader saw
# last — which is not a decision anybody made.
action = {
    id    = unlock_the_hatch
    match = { "unlock hatch" }

    successMsg = @platform(qt, web, cli) "The lock yields with a soft chime."
    successMsg = @platform(glk, cli)     "The lock yields."
}

# The same rule with no annotation on one side. A statement carrying no
# `@platform` runs on every frontend, so it overlaps every gated one: this is
# a duplicate rather than a default with an exception, because §5.4.1 gives
# no fallback precedence to infer one from.
action = {
    id    = seal_the_hatch
    match = { "seal hatch" }

    successMsg = "The hatch seals."
    successMsg = @platform(glk) "The hatch seals with a clunk."
}
