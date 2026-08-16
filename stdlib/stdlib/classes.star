# =============================================================================
#  stdlib — the kinds a game is built from
# =============================================================================
#
#  Every class here derives, directly or eventually, from `starcore.object`
#  (spec §8.1.1). None of them is core-owned: `thing`, `container` and
#  `person` are this library's policy, and a ruleset that wanted a different
#  set could declare one and never load this file.
#
#  Note `room` below. `starcore.room` is core-owned — scope is computed from
#  an actor's room and the location slot resolves to one — but the `room` an
#  author writes is this library's, deriving from it. That is the whole shape
#  of the §7.2.2 boundary in one declaration: core owns the concept it reads,
#  the library owns the thing authors use.
# =============================================================================

# The ways out of a room. Spec §6.6.1 writes `exits` as `map<direction, ref<room>>`
# and reads `location.exits.north` as a map key, which only works if `direction`
# is a declared enum — `exits.nrth` is a compile error naming this set rather
# than a door that silently is not there.
#
# The twelve below are the conventional set; a ruleset wanting shipboard
# `fore` and `aft` declares its own `direction` and replaces this one, which
# is what a library owning a vocabulary means.
enum = {
    id     = direction
    values = { north south east west
               northeast northwest southeast southwest
               up down in out }
    doc    = "A way out of a room (§6.6.1)."
}

class = {
    id       = room
    of_class = starcore.room
    doc      = "A place the player can be in."

    prop_def = {
        dark       = bool
        exits      = map<direction, ref<room>>
        first_seen = bool
    }
    dark = no
}

class = {
    id  = thing
    doc = "A physical object. The default kind for anything that is not a place."

    prop_def = {
        portable    = bool
        description = text
        weight      = int
    }
    portable = yes
    weight   = 1
}

class = {
    id       = container
    of_class = thing
    traits   = { openable }
    doc      = "Holds other things, with the `in` relation."

    prop_def = {
        capacity         = int
        holding_relation = enum<relation_enum>
    }
    capacity         = 10
    holding_relation = in
}

class = {
    id       = supporter
    of_class = thing
    doc      = "Holds other things on top of it, with the `on` relation."

    prop_def = {
        holding_relation = enum<relation_enum>
    }
    holding_relation = on
    portable         = no
}

class = {
    id       = door
    of_class = thing
    traits   = { openable lockable }
    doc      = "Joins two rooms, and is referable from both of them via presence (§8.6)."

    portable = no
}

class = {
    id       = backdrop
    of_class = thing
    doc      = "Scenery present in many rooms at once: the sky, the ground, a distant hum."

    portable = no
}

class = {
    id       = person
    of_class = thing
    traits   = { starcore.actor }
    doc      = "Something that takes turns. The actor loop iterates these."

    prop_def = {
        proper_name = bool
    }
    portable    = no
    proper_name = yes
}
