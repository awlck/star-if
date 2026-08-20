# =============================================================================
#  stdlib — default messages
# =============================================================================
#
#  Every `$key` the actions in actions.star name, in one place. Until now they
#  named keys nothing declared, and a game that did not happen to define them
#  itself would have rendered «taken_default» in play — the visible fallback
#  of spec §9.6, doing exactly the job it exists to do, on a library that
#  should never have needed it.
#
#  WHY A LIBRARY DECLARES ITS OWN AND A GAME MAY STILL OVERRIDE THEM.
#  §9.6 requires localisation keys to be unique within a language, and the
#  scope of that uniqueness is one file: two entries in one table are an
#  ambiguity nothing can resolve, while a later file superseding an earlier
#  one is what load order means everywhere else in the format (§13.2). The
#  names below say so out loud — a `taken_default` is a default, and a game
#  that wants its own writes its own.
#
#  Language-neutral by construction: this file is `en`, and a translation is
#  another `loc` block rather than another engine (§9.5.4).
# =============================================================================

loc = {
    lang = en

    # take / drop
    take_not_portable  = "[The noun] [is(noun)] not something you can carry."
    taken_default      = "[The actor] [verb(actor, take)] [the noun]."
    drop_not_held      = "[The actor] [is(actor)] not holding [the noun]."
    dropped_default    = "[The actor] [verb(actor, drop)] [the noun]."

    # open / close
    already_open       = "[The noun] [is(noun)] already open."
    its_locked         = "[The noun] [is(noun)] locked."
    opened_default     = "[The actor] [verb(actor, open)] [the noun]."
    already_closed     = "[The noun] [is(noun)] already closed."
    closed_default     = "[The actor] [verb(actor, close)] [the noun]."
}
