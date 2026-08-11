# EXPECT W-FAILMSG-MISSING
# spec §10.5.3 — a restriction that fails without a message forces the engine
# onto its generic fallback, which is the main way a game feels unfinished.
# Warning rather than error, because the action could have supplied one.
action = {
    id = silent_refusal
    restrictions = {
        noun = { has_trait = portable }
    }
}
