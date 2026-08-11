# EXPECT E-FAILMSG-SILENT
# spec §10.5 — `conditions` and `when` are silent stages by definition. A
# message written there is one the author never sees fail.
rule = {
    of_action  = take
    when       = { noun = { has_trait = portable } }
    conditions = { actor = { strength >= 10
                             failureMsg = "Never printed." } }
}
