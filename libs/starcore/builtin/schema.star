# =============================================================================
#  starcore built-in schemas — the core-owned forms of spec §7.2.4
# =============================================================================
#
#  These describe the data `starcore` itself reads and writes at RUN time: the
#  turn sequence, residency and streaming, the game's own manifest, and the
#  text layer's styles and localisation tables. They are not the standard
#  library's to define, and §7.2.2 explains at length why that distinction is
#  worth drawing — ADRIFT 5 needs the library to create the location properties
#  the system uses, and Inform 7 attaches special handling to the eighth action
#  declared. Both are engines depending on a convention while pretending the
#  library is free.
#
#  §7.2.4 asks two questions, not one. This file answers the second: *who
#  reads the data*. The first — *who parses the declaration* — is answered by
#  libs/stardata/builtin/format.star, which holds `class`, `trait`, `enum`,
#  `global`, `const`, `library` and the rest of the forms the format layer
#  parses with readers of its own. Those used to live here, stated twice: once
#  as a declaration and once as a hard-coded reader, with nothing checking that
#  the two agreed. They had already drifted.
#
#  So: every schema here is `sealed = yes`. A library may add keys to one
#  through `schema_extension` (§7.5) and properties to a core class through
#  `class_extension` (§8.2). It may not redefine, retype, remove or supersede.
#
#  Phase 0 loads these from disk so the validator can be developed against
#  them. Phase 1 embeds them in the binary via a generated string literal, so
#  they stay one source of truth, diffable, and impossible to ship without.
# =============================================================================


# --- Text: styles and localisation -------------------------------------------

# Both of these are core-owned by §7.2.4's own test — "does `starcore`'s own
# code read or write it?" — and libs/starcore/src/text.cpp is that code.
#
# Appendix C used to list them under "forms supplied by `stdlib` unless
# noted", which was true of neither. A `style` declared by a library core
# could not name would leave §5.4.1's `@style(id)` annotation with an
# argument nothing can check, and a `loc` table core could not read would
# leave §9.6's fallback chain — the loaded locale, then the source language,
# then a visible «key» — with nothing to fall back through. Neither is a
# convention core could merely hope a library follows, which is precisely the
# test §7.2.2 exists to make.

schema = {
    id        = style
    top_level = yes
    sealed    = yes
    doc       = "A semantic text style a theme maps to concrete attributes (§9.3)."

    key = { name = id   type = identifier  required = yes  unique_in = style
            doc  = "The name a style annotation carries, on a value or inside a template." }
    key = { name = doc  type = text }
}

schema = {
    id        = loc
    top_level = yes
    sealed    = yes
    # Every key but `lang` is a localisation key, so the key set is whatever
    # the game says rather than anything core can list — the same reason
    # `version_constraints` below is open. It also means a loc entry has no
    # declared type, which is why the templates in one are parsed by
    # libs/starcore/src/text.cpp rather than by the type checker: §9.6 makes
    # them templates by definition, and there is no `type = text` to say so.
    open      = yes
    doc       = "The localisation table for one language (§9.6)."

    key = { name = lang  type = identifier  required = yes
            doc  = "The language this table is written in." }
}

# --- The turn sequence and dispatch index ------------------------------------

# Proposal §7.2: an action either consumes a round when it succeeds, always,
# or never. `never` is what "out of world" actions are — checking your
# inventory does not give the enemy a free swing.
enum = {
    id     = advances_turn_enum
    values = { on_success always never }
    doc    = "Whether an action consumes a round (proposal §7.2)."
}

schema = {
    id        = action
    top_level = yes
    sealed    = yes
    doc       = "Something the player can attempt, and what happens when they do."

    # §8.8.3: the stages a narrowing flows forward through, in order. Declared
    # here rather than built into the analysis, so that `libs/stardata` runs
    # the dataflow without knowing that a thing called `restrictions` exists —
    # which is what keeps the one genuinely novel static analysis in Phase 0
    # on the mechanism side of proposal §2.1.1. A ruleset adding a stage gets
    # narrowing through it by declaring it here.
    stage_order = { conditions restrictions effects successMsg failureMsg }

    key = { name = id            type = identifier          required = yes  unique_in = action }
    key = { name = match         type = list<string>        required = yes
            doc  = "The parser grammar lines this action answers to." }
    key = { name = conditions    type = condition_block     combine = smart }
    key = { name = restrictions  type = condition_block     combine = smart }
    key = { name = effects       type = effect_block        combine = override }
    key = { name = successMsg    type = text_or_script      combine = override }
    key = { name = failureMsg    type = text_or_script      combine = override
            doc  = "Shown when a restriction fails; §10.5 governs where it may go." }
    key = { name = duration      type = duration            default  = default }
    key = { name = advances_turn type = enum<advances_turn_enum>  default = on_success }
    key = { name = rule          type = block<rule>         arity = many }
    key = { name = doc           type = text }
}

schema = {
    id        = rule
    top_level = yes
    sealed    = yes
    doc       = "A response to an action or an event, run when its conditions hold."

    # A rule opens with `when`, which an action has no equivalent of: the
    # action is already chosen by the time its own stages run (§8.8.3).
    stage_order = { when conditions restrictions effects successMsg failureMsg }

    key = { name = id            type = identifier }
    key = { name = of_action     type = ref<action>      exclusive_group = subject
            doc  = "The action this rule responds to." }
    key = { name = of_event      type = identifier       exclusive_group = subject
            doc  = "The event this rule responds to." }
    key = { name = when          type = condition_block  combine = smart }
    key = { name = conditions    type = condition_block  combine = smart }
    key = { name = restrictions  type = condition_block  combine = smart }
    key = { name = effects       type = effect_block     combine = override }
    key = { name = successMsg    type = text_or_script   combine = override }
    key = { name = failureMsg    type = text_or_script   combine = override }
    key = { name = priority      type = int }
    key = { name = doc           type = text }
}

schema = {
    id        = turn_hook
    top_level = yes
    sealed    = yes
    doc       = "Runs at a fixed point in the turn sequence, independently of any action."

    stage_order = { when effects }

    key = { name = id       type = identifier       required = yes  unique_in = turn_hook }
    key = { name = when     type = condition_block  combine = smart }
    key = { name = effects  type = effect_block     combine = override }
    key = { name = doc      type = text }
}


# --- Residency and streaming -------------------------------------------------

schema = {
    id        = sector
    top_level = yes
    sealed    = yes
    doc       = "A unit of residency: what is loaded together and streamed together."

    key = { name = id               type = identifier  required = yes  unique_in = sector }
    key = { name = always_resident  type = bool
            doc  = "Never streamed out, however far the player travels." }
    key = { name = doc              type = text }
}


# --- Load order --------------------------------------------------------------

schema = {
    id        = project
    top_level = yes
    sealed    = yes
    doc       = "The project manifest, one per project, at its root (§13.1)."

    key = { name = id               type = identifier        required = yes }
    key = { name = title            type = text }
    key = { name = author           type = text }
    key = { name = version          type = string }
    key = { name = ifid             type = string }
    key = { name = source_language  type = identifier }
    key = { name = uses             type = list<identifier>
            doc  = "Libraries to load, in declaration order (§13.2)." }
    key = { name = player           type = ref<starcore.object> }
    key = { name = start_room       type = ref<starcore.room> }
    key = { name = entry_sector     type = ref<sector> }
    key = { name = defaults         type = block<project_defaults> }
    key = { name = simulation       type = block<project_simulation> }
    key = { name = doc              type = text }
}


# --- Nested block shapes -----------------------------------------------------


# The three nested shapes the manifests use (§13.1, §13.3). Each was named by
# a `block<...>` type before it was declared, which is a key nothing could
# ever check — the type checker of §6.2 reports exactly that, and this is the
# other half of the fix.

schema = {
    id     = project_defaults
    sealed = yes
    doc    = "Game-level defaults a project sets once, per §13.1."

    key = { name = action_duration  type = duration
            doc  = "The duration an action takes when it declares none." }
    key = { name = advances_turn    type = enum<advances_turn_enum>
            doc  = "The default for an action that does not say (proposal §7.2)." }
}

schema = {
    id     = project_simulation
    sealed = yes
    doc    = "How much the world simulates while the player is elsewhere (§13.1)."

    # `offstage_default` is an identifier and not an enum because one of its
    # four values is `none`, which §3.9 reserves — so the set cannot be
    # declared as an enum until that is resolved.
    key = { name = offstage_default         type = identifier
            doc  = "none, catch_up, simulate or continuous (proposal §5.3)." }
    key = { name = simulate_max_rounds      type = int
            doc  = "Beyond this many rounds, fall back to catch_up." }
    key = { name = simulate_time_budget_ms  type = int }
    key = { name = simulate_progress        type = bool }
    # A value of whatever enum the ruleset declares for it. Core does not own
    # that vocabulary, so it does not type it.
    key = { name = default_combat_response  type = identifier }
}

