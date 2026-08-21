# EXPECT E-SCHEMA-INVALID
# spec §8.2, §8.3 — a `class_extension` names its target with `of_class` or
# with `of_trait`, and the two look in different namespaces. §8.3 keeps
# classes and traits apart, so an id may legally be both; a single lookup that
# tried one and fell back to the other would extend whichever it found first,
# which is not a decision anybody wrote down.
#
# `glowing` is a trait, so `of_class` finds nothing. The diagnostic says so in
# those terms rather than "nothing declares it", because an author who wrote
# the wrong key has not misspelled anything and would go looking for a typo
# that is not there.
trait = {
    id       = glowing
    prop_def = { lumens = int }
}

class_extension = {
    of_class = glowing
    prop_def = { colour = text }
}
