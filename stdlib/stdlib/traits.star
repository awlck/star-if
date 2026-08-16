# =============================================================================
#  stdlib — traits
# =============================================================================
#
#  Orthogonal capabilities that cut across the class tree, which is exactly
#  what single inheritance cannot express (spec §8.3). None of these is
#  core-owned: an engine that knows the name `open` is the ADRIFT failure
#  §7.2.2 describes. What core knows is that *some* properties affect scope,
#  and it is told which by a marker (§7.2.3) — see `affects_scope` below.
# =============================================================================

trait = {
    id  = openable
    doc = "Can be opened and closed."

    prop_def = {
        # A marker, not a magic name (§7.2.3). The engine never learns that a
        # property called `open` decides what is in scope; it learns that some
        # properties do, and this says which. A ruleset whose equivalent is
        # `shuttered` gets the same behaviour by declaring the same marker.
        open              = { type = bool  affects_scope = yes }
        openable_by_hand  = bool
    }
    open             = no
    openable_by_hand = yes
}

trait = {
    id  = lockable
    doc = "Can be locked, and needs a key to unlock."

    prop_def = {
        locked   = { type = bool  affects_scope = yes }
        key_item = ref<starcore.object>
    }
    locked = no
}

trait = {
    id  = wearable
    doc = "Can be worn, which is the `worn` containment relation (§8.5)."

    prop_def = {
        worn_on = identifier
    }
}

trait = {
    id  = edible
    doc = "Can be eaten, and is destroyed by it."

    prop_def = {
        nourishment = int
    }
    nourishment = 1
}

trait = {
    id  = lit
    doc = "Emits light, whether or not it is switched on."

    prop_def = {
        lit_radius = { type = int  affects_scope = yes }
    }
    lit_radius = 1
}
