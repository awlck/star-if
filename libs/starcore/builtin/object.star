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


# Deliberately NOT `sealed`, unlike `advances_turn_enum` in schema.star: these
# seven values are vocabulary the placement code
# (libs/starcore/src/placement.cpp) reads generically off this enum rather
# than switching on by name (backlog F2c), so a ruleset with a different
# spatial vocabulary may supersede it wholesale with `@replaces(starcore)`
# and get its own keywords working as sugar with no code change — see the
# test at the bottom of tests/unit/starcore/placement_test.cpp.
enum = {
    id     = relation_enum
    values = { in on under behind carried worn part_of }
    doc    = "How an object is held by its containment parent (§8.5)."
}


# §8.6: either an explicit room set, or a query resolved at compile time
# (unless `dynamic = yes`, in which case its predicate is evaluated when
# scope is computed instead). `rooms`/`where` is an exclusive_group rather
# than two differently-typed spellings of `present_in` itself, because a key
# can only have one declared type (§7.2) -- the list-vs-query choice has to
# be a choice between two KEYS of one nested shape, not two shapes for one
# key. `dynamic` only means anything alongside `where`; nothing here enforces
# that yet, so it is silently inert alongside `rooms` rather than rejected.
schema = {
    id     = presence
    sealed = yes
    doc    = "Either shape present_in accepts (§8.6)."

    key = { name = rooms    type = set<ref<starcore.room>>  exclusive_group = source
            required = yes
            doc      = "An explicit list of rooms this object is present in." }
    key = { name = where    type = condition_block          exclusive_group = source
            required = yes
            doc      = "A query resolving to a room set." }
    key = { name = dynamic  type = bool  default = no
            doc      = "Re-evaluate `where` at scope time instead of once at compile time." }
}


class = {
    id     = starcore.object
    root   = yes
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
        present_in  = block<presence>

        # What the parser matches and the templates print. `disambiguation_name`
        # is proposal §6.4.1's authored answer to two objects sharing every
        # name: optional, and checked only by the (unimplemented)
        # W-NAMES-SUBSET warning that steers an author toward setting it -- but
        # an author cannot set what no property declares, so it travels with
        # `name` and `synonyms` rather than waiting for that warning to land.
        name                = text
        synonyms            = list<identifier>
        disambiguation_name = text
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
