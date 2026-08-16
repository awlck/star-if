# =============================================================================
#  starcore built-in classes and traits — spec §8.1.1 and §7.2.4
# =============================================================================
#
#  The properties below are not conveniences. They are the fields of the world
#  store (proposal §5.2) under author-visible names, which is what lets
#  `starcore` implement `held_by`, `carrying` and `containing` in C++ against
#  a known layout instead of walking whatever a library happened to call its
#  parent pointer.
#
#  A library MAY add properties to any of these with `class_extension`. It MUST
#  NOT retype or remove one — see §8.1.1's last line, and F2a's assertions.
# =============================================================================


enum = {
    id     = relation_enum
    values = { in on under behind carried worn part_of }
    doc    = "How an object is held by its containment parent (§8.5)."
}


class = {
    id     = starcore.object
    sealed = yes
    doc    = "The root class. Every world object is one, as every Java type is an Object."

    prop_def = {
        # Containment (§8.5). One parent, and the relation that parent link
        # carries. `in = ornate_box` is sugar for setting both (backlog F2c).
        holder      = ref<starcore.object>
        relation    = enum<relation_enum>

        # Residency (§8.6.2 of the proposal): what streams together.
        sector      = ref<sector>

        # Presence (§8.6): being referable from several rooms at once without
        # being contained by any of them. A door is the reason this exists.
        present_in  = set<ref<starcore.room>>

        # What the parser matches and the templates print.
        name        = text
        synonyms    = list<identifier>
    }
}


class = {
    id       = starcore.room
    of_class = starcore.object
    sealed   = yes
    doc      = "A place. Scope is computed from an actor's room, and the location slot resolves to one."
}


trait = {
    id     = starcore.actor
    sealed = yes
    doc    = "Something the actor loop iterates. `busy_until` is why this is core-owned and not library policy."

    prop_def = {
        busy_until = int
    }
}
