# EXPECT E-TYPE-MISMATCH
# spec §6.2 — `dice` and `clock_time` carry structure inside a string, and
# each is "parsed at compile time". The alternative is discovering that "3d"
# names no die when somebody rolls it, and that "25:9" is not a time when the
# calendar tries to resolve it.
#
# The hour is deliberately not range-checked: §11.6 resolves a clock_time
# against the sector's calendar, and a sector may declare a `local_clock`, so
# whether hour 25 exists is not this pass's question. The shape is.
schema = {
    id        = alarm
    top_level = yes

    key = { name = id       type = identifier  required = yes  unique_in = alarm }
    key = { name = damage   type = dice }
    key = { name = goes_off type = clock_time }
}

alarm = {
    id       = klaxon
    damage   = "3d"
    goes_off = "25:9"
}
