# EXPECT E-BLOCK-MIXED
# spec §8.5 — `in = X` is sugar for `holder = X  relation = in`. Writing both
# spellings assigns the same two slots twice, and there is no sensible
# precedence between them. Detecting it needs the schema layer; this fixture
# pins the syntax and fails today on a shape error.
thing = {
    id       = confused_key
    in       = ornate_box
    holder   = mess_table
    relation = on
    stray
}
