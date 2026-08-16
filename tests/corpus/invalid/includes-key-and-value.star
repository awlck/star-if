# EXPECT E-BLOCK-MIXED
# spec §6.5.1 — `includes` takes a value OR a key, never both. An author
# writing both almost certainly means "is entry K equal to V", which is a
# different question with a better spelling: the path form
# `location.exits.north == corridor`, or `map_get` inside `compare` when the
# key is computed. Enforcing the exclusive group needs the schema layer;
# this fixture pins the syntax and fails today on a shape error.
rule = {
    of_action  = look
    when       = { }
    conditions = {
        includes = { collection = location.exits  key = north
                     value = corridor  stray }
    }
}
