# EXPECT E-PLACEMENT-CONFLICT
# spec §8.5 — `in = X` is sugar for `holder = X  relation = in`. Writing both
# spellings assigns the same two slots twice, and there is no sensible
# precedence between them, so neither wins: guessing would put the object
# somewhere the author did not ask for.
thing = {
    id       = confused_key
    in       = ornate_box
    holder   = mess_table
    relation = on
}
