# EXPECT E-FAILMSG-SILENT
# spec §10.5 — `conditions` and `when` are silent stages by definition. A
# message written there is one the author never sees fail.
class_extension = { of_class = person  prop_def = { strength = int } }

rule = {
    of_action  = take
    when       = { noun = { has_trait = portable } }
    conditions = { actor = { of_class = person  strength >= 10
                             failureMsg = "Never printed." } }
}
