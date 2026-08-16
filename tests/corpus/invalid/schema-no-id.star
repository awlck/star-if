# EXPECT E-SCHEMA-INVALID
# spec §7.2 — a schema with no id describes nothing. Reported rather than
# ignored, because a schema that silently fails to register takes every
# check it was meant to perform with it.
schema = {
    top_level = yes
    key = { name = id  type = identifier  required = yes }
}
