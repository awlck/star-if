# EXPECT E-CORE-REPARENT
# spec §8.2 — a class_extension names its target with `of_class`, so changing
# the parent is spelled with a second one. It is refused: re-pointing a class
# silently rewrites every object of that class, including ones written by
# somebody else.
class_extension = {
    of_class = starcore.room
    of_class = starcore.actor
}
