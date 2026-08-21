# EXPECT E-GLOBAL-UNDECLARED
# EXPECT W-GLOBAL-UNUSED
# spec §6.4 — "Both MUST be declared. There is no implicit creation." An
# effect writing a global nobody declared creates nothing; it writes into a
# name the save format has no slot for, and the condition that was meant to
# read it back never sees a value.
global = { id = alert_level  type = int  initial = 0 }

rule = {
    of_action = examine
    when      = { }
    effects   = { add_global = { id = alert_levl  amount = 1 } }
}
