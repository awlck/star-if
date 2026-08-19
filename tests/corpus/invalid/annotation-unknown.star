# EXPECT E-UNKNOWN-ANNOTATION
# spec §3.8 — "an unknown annotation MUST be an error, since silently
# ignoring one changes behaviour invisibly". `@prepend` is a plausible
# spelling of a real thing: §5.4.1 calls it `@before`. Ignored, it would
# leave the author with a message that replaces the inherited one instead of
# preceding it, and nothing on screen to explain the difference.
rule = {
    of_action  = examine
    successMsg = @prepend "You turn it over in your hands."
}

# §15 reserves `@deprecated`, `@since` and `@experimental` "so that adding
# them later is not a breaking change" — which only holds if writing one
# today is refused rather than ignored. The same code, and a note that says
# what happened, on the `?=` precedent (§6.3.1).
rule = {
    of_action  = take
    successMsg = @since "You take it."
}
