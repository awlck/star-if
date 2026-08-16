# EXPECT E-KEY-MISSING
# spec §7.2 — `values` is required on an enum. A form declaring a required
# key and not getting it is caught at load, not when something first reads
# the missing value.
enum = {
    id = mood_enum
}
