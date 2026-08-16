# EXPECT E-PROPDEF-TYPE-MISMATCH
# spec §7.2.2 — adding to a core class is always permitted; retyping one of
# its properties is not. starcore reads `holder` as a reference at a known
# layout, so making it an int would corrupt data rather than merely surprise.
class_extension = {
    of_class = starcore.object
    prop_def = {
        holder = int
    }
}
