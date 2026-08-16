# EXPECT E-PROPDEF-TYPE-MISMATCH
# spec §7.5 — `schema_extension` adds keys. Redeclaring an existing key with a
# different declaration is a redefinition wearing an extension's clothes, and
# on a sealed form it is exactly what sealing exists to prevent.
schema_extension = {
    of_schema = action
    key = { name = id  type = int }
}
