# EXPECT E-SCHEMA-INVALID
# spec §6.2 — a key declared with a type nobody declares is a key nothing can
# ever check: a schema wearing the appearance of one. `mood_enum` is not
# declared here or anywhere, so `mood` accepts whatever it is given.
#
# Reported at the schema, where the mistake was made, rather than at every
# value written against it.
schema = {
    id        = journal_entry
    top_level = yes

    key = { name = id    type = identifier  required = yes  unique_in = journal_entry }
    key = { name = mood  type = enum<mood_enum> }
}
