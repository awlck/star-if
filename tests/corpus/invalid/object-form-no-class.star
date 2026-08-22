# EXPECT E-KEY-MISSING
# EXPECT E-REF-UNRESOLVED
# spec §7.4 — the long object spelling names its class inside, with `of_class`,
# where the short spelling puts it on the left. That key is `required = yes` on
# the `object` form, so leaving it out is E-KEY-MISSING like any other missing
# required key: the point of declaring the form is that this rule is data
# rather than another branch in the loader.
object = {
    id = brass_key
}

# And `of_class` is typed `ref<class>`, so a class nobody declares is the same
# unresolved reference `of_action` and `holder` would be — with the same
# suggestion (§6.2, backlog F9).
object = {
    id       = ornate_box
    of_class = thign
}
