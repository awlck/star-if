# EXPECT E-FLAG-UNDECLARED
# EXPECT W-GLOBAL-UNUSED
# spec §6.4.1 — flags are sugar over declared bool globals. As undeclared
# strings, `set_flag = captain_found` paired with `flag_set = captain_finded`
# is a silent permanent bug: the condition simply never fires and nothing
# reports it. The declaration requirement makes the typo a compile error.
global = { id = captain_found  type = bool  initial = no }

rule = {
    of_action = examine
    when      = { }
    effects   = { set_flag = captain_found }
}

rule = {
    of_action  = go
    when       = { }
    conditions = { flag_set = captain_finded }
}

# The unused-global warning is the same bug seen from the other side: the
# only place that would have read `captain_found` reads something else, so
# nothing reads it at all (spec §6.4).
