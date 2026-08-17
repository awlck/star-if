# EXPECT E-CORE-REPARENT
# EXPECT E-DUP-KEY
# spec §8.2 — a class_extension names its target with `of_class`, so changing
# the parent is spelled with a second one. It is refused: re-pointing a class
# silently rewrites every object of that class, including ones written by
# somebody else.
#
# Two codes, and deliberately so. `of_class` is declared `arity = one`, so a
# second binding of it is a duplicate key (§5.3) whatever the values are —
# that is the general rule, and it fires here as it would anywhere. The
# reparent error is the specific reading of the same two lines, and it is the
# one that says why the obvious fix is not available.
class_extension = {
    of_class = starcore.room
    of_class = starcore.actor
}
