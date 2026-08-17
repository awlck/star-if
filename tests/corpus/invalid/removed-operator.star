# EXPECT E-OP-REMOVED
# spec §6.3.1, §15 — `?=` appeared in published drafts and was removed. It is
# still lexed as one token so that a file written against an older draft gets
# told what happened, rather than a complaint about an unknown character.
class_extension = {
    of_class = thing
    prop_def = { notes = string }
    notes   ?= "was this bound? you cannot tell from here, which was the problem"
}
