# EXPECT E-EXCLUSIVE-MISSING
# spec §7.2.1 — "zero is an error if any member is `required`". A group's
# members are alternatives, so requiredness belongs to the group rather than
# to either key: this block is refused for setting neither, and a block
# setting one of them is not refused for missing the other.
#
# The form is declared here rather than borrowed from the core set, because
# no core form has a required group today — `rule`'s `of_action` / `of_event`
# pair is optional, since a rule nested in an action needs neither.
schema = {
    id        = salvage_table
    top_level = yes
    doc       = "Where the wreck's loot comes from: an explicit list, or another table."

    key = { name = id      type = identifier        required = yes  unique_in = salvage_table }
    key = { name = drops   type = list<identifier>  required = yes  exclusive_group = source
            doc  = "The items themselves." }
    key = { name = copies  type = identifier        required = yes  exclusive_group = source
            doc  = "Another salvage_table to take the items from." }
}

salvage_table = {
    id = shipwreck_hold
}
