# EXPECT E-REF-UNRESOLVED
# spec §6.2 — `ref<C>` is "a reference to an object of class `C` or a
# subclass; validated at compile time", and §14.3 makes an unresolvable one an
# error. Backlog F9 is what made the validation real; before it, any identifier
# satisfied any `ref`, so a renamed room and a typo were indistinguishable from
# a working link.
#
# TWO NAMESPACES, one per §7.2.4's two kinds of `ref` target.

room = { id = your_cell }

# A reference to an OBJECT. `exits` is `map<direction, ref<room>>`, so the
# value has to name a room somebody declared. This one is a door onto nothing:
# at run time the player walks north into a room that was never built.
room = {
    id    = corridor
    exits = { north = your_cel }
}

# A reference to an INSTANCE OF A FORM. `of_action` is `ref<action>`, resolved
# in the namespace `action` declares itself unique in (§7.2, §7.6). A rule
# bound to an action nobody declares never fires, and nothing at run time is
# ever going to mention it.
action = { id = examine  match = { "examine [something]" } }

rule = {
    of_action = examnie
    effects   = { }
}

# `none` is not a name to resolve: §5.5 makes it "explicitly empty", and
# assigning it to a `ref<C>` clears the slot. Neither line below is an error.
thing = { id = loose_bolt  holder = none }
thing = { id = spare_bolt  holder = inherit }
