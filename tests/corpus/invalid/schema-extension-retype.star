# EXPECT E-BLOCK-MIXED
# spec §7.5 — `schema_extension` adds keys. Redeclaring an existing key with a
# different declaration is a redefinition wearing an extension's clothes, and
# on a sealed form it is exactly what sealing exists to prevent.
#
# The real code is E-PROPDEF-TYPE-MISMATCH, which needs the schema layer.
schema_extension = {
    of_schema = action
    key = { name = id  type = int  stray }
}
