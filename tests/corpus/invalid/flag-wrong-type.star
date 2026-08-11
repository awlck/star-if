# EXPECT E-FLAG-NOT-BOOL
# spec §6.4.1 — a flag must name a global of type bool. Setting a non-bool
# global through the flag sugar would silently coerce.
global = { id = alert_level  type = int  initial = 0 }

rule = {
    of_action = examine
    when      = { }
    effects   = { set_flag = alert_level }
}
