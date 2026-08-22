# EXPECT E-SCHEMA-DUPLICATE
# spec §7.4, §7.6 — object ids are ONE namespace, and it is implied rather
# than declared: no `schema` describes an instantiation, so there is no
# `unique_in` key to read the namespace out of. It exists anyway, because the
# alternative is nonsense. `ref<C>` resolves an id to an object (§6.2), and
# §6.6's paths and §11.1's effects name one by id alone without ever saying
# what class they expect — so two objects answering to `airlock` leave every
# reference to it undefined.
#
# ONE NAMESPACE ACROSS CLASSES, not one per class, for the same reason: the
# id is all a reference carries.
room  = { id = airlock }
thing = { id = airlock }

# And the escape, which is §7.6's and needs no new machinery: a mod that means
# to supersede the game's object says so, and is believed. Nothing below is an
# error.
thing = { id = brass_key }
