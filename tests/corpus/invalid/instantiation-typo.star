# EXPECT E-UNKNOWN-KEY
# proposal §4.9, and the worked example that argues for the whole schema
# layer: a class declares `outdoors_room` and an instantiation writes
# `outdoor_room`. Under §7.4 that is an error pointing at the exact span with
# a "did you mean `outdoors_room`?" suggestion. In an untyped
# Clausewitz-style loader it is a silently ignored block and a room that
# mysteriously doesn't exist — which is the failure mode this whole workstream
# exists to make impossible.
class = {
    id       = outdoors_room
    of_class = starcore.room
}

outdoor_room = {
    id   = antecourt
    name = "Antecourt"
}

# The same rule one level down: a key that resembles a declared one. §7.3
# asks for the suggestion by name, computed by edit distance "against the
# declared keys" — so the candidates come from the schema that refused it and
# not from every identifier in the file.
sector = {
    id             = the_bridge
    always_residnt = yes
}
