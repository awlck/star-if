# EXPECT E-BLOCK-MIXED
# spec §5.2 — a block holds either bare scalars or statements, never both.
# The usual cause is a missing '='.
class = {
    id = confused
    traits = { openable  lockable = yes }
}
