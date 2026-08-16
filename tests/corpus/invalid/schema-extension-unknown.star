# EXPECT E-SCHEMA-INVALID
# spec §7.5 — extending a form that does not exist is an error naming it,
# rather than quietly creating one. A typo in `of_schema` would otherwise
# produce a schema nothing validates against and no diagnostic at all.
schema_extension = {
    of_schema = acton
    key = { name = stamina_cost  type = int }
}
