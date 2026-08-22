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

# Declared so that the only thing wrong with this file is the one thing it
# is about: a rule bound to an action nobody declares is its own error,
# and backlog F9 now reports it (§6.2, §14.3).
action = { id = polish_the_bell  match = { "polish the bell" } }
