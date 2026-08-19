# EXPECT E-ANNOT-CONFLICT
# spec §5.4 — "Contradictory combinations (`@before` `@after`) MUST be
# rejected". Each of the five combining annotations answers one question,
# what this value does to the value it inherits, so two of them leave the
# answer undecidable: there is no order of application under which a block
# runs both before and after the inherited one.
rule = {
    of_action    = open
    restrictions = @before @after {
        noun = { locked == no  failureMsg = $its_locked }
    }
}

# The same rule catches a repeat, which is likelier than the contradiction:
# an author moving an annotation from one line to another and leaving the
# original behind.
rule = {
    of_action = go
    effects   = @override @override { }
}
