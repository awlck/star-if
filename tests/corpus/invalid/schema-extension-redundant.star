# EXPECT W-PROPDEF-REDUNDANT
# spec §7.5 — two libraries may both want a key and both declare it, and that
# is redundancy rather than conflict: an extension forbids disagreement, not
# agreement. A warning, because the line does nothing and saying so is worth
# more than refusing a file over it.
schema_extension = {
    of_schema = sector
    key = { name = always_resident  type = bool }
}
