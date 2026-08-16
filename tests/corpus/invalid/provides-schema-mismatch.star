# EXPECT W-PROVIDES-MISMATCH
# spec §13.3 — `provides_schema` is a manifest, listing the forms a library
# contributes so the editor's library browser and a reader can see them in
# one place. It declares nothing; a mismatch against what the library really
# declares is a warning, because the manifest is what people go by.
library = {
    id              = ghost_forms
    version         = "1.0.0"
    provides_schema = { stat_block loot_table }
}
