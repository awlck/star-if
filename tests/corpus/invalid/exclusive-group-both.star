# EXPECT E-EXCLUSIVE-GROUP
# spec §7.2.1 — `of_action` and `of_event` are alternative answers to one
# question ("what does this rule respond to?"), declared as one
# `exclusive_group` on the core `rule` form. Writing both leaves nothing to
# decide between them, so it is an error naming the group's members.
#
# Earlier drafts stated this in prose and gave the schema no way to express
# it, which meant three forms documented mutually exclusive arguments that
# nothing enforced.
rule = {
    id        = ring_the_bell
    of_action = polish_the_bell
    of_event  = bell_rang
    effects   = { }
}
