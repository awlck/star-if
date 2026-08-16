# EXPECT E-BLOCK-MIXED
# spec §7.2.2 — core-owned schemas are sealed. A library redefining `class`
# would silently change the shape of data starcore reads directly, which is
# the ADRIFT/Inform failure mode this rule exists to prevent: an engine that
# depends on a convention while presenting it as the library's choice.
# Sealing needs the schema layer; this fixture pins the syntax and fails today
# on a shape error.
schema = {
    id        = class
    top_level = yes
    key = { name = id  type = identifier  required = yes  bogus }
}
