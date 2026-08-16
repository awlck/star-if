# EXPECT E-SCHEMA-SEALED
# spec §7.2.2, §8.1.1 — starcore.object's properties are the fields of the
# world store under author-visible names, not a convention a library may
# replace. `class_extension` is the sanctioned way to add to it.
class = {
    id = starcore.object
    prop_def = {
        holder = int
    }
}
