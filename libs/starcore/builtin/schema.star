# =============================================================================
#  starcore built-in schemas — the forms of spec §7.2.4
# =============================================================================
#
#  These describe the data `starcore` itself reads and writes: the containment
#  tree, the class of an object, the turn sequence, the load order. They are
#  not the standard library's to define, and §7.2.2 explains at length why
#  that distinction is worth drawing — ADRIFT 5 needs the library to create
#  the location properties the system uses, and Inform 7 attaches special
#  handling to the eighth action declared. Both are engines depending on a
#  convention while pretending the library is free.
#
#  So: every schema here is `sealed = yes`. A library may add keys to one
#  through `schema_extension` (§7.5) and properties to a core class through
#  `class_extension` (§8.2). It may not redefine, retype, remove or supersede.
#
#  The one schema NOT in this file is the schema for `schema` itself, which
#  is hard-coded in libs/stardata/src/schema/schema.cpp because it is what
#  reads this file. It is kept as small as the bootstrap allows, since
#  anything in it is a rule nobody can see in the source.
#
#  Phase 0 loads these from disk so the validator can be developed against
#  them. Phase 1 embeds them in the binary via a generated string literal, so
#  they stay one source of truth, diffable, and impossible to ship without.
# =============================================================================


# --- The schema layer itself -------------------------------------------------

schema = {
    id        = class
    top_level = yes
    sealed    = yes
    # §8.1: any key other than the ones below sets the class's default value
    # for a property of that name. Which properties exist is a question for
    # the object model, not for the key checker, so the form is open.
    open      = yes
    doc       = "A kind of world object. One parent, named by of_class; single inheritance."

    key = { name = id        type = identifier       required = yes  unique_in = class
            doc  = "The name this class is instantiated by, and referred to by." }
    key = { name = of_class  type = identifier
            doc  = "The parent class. Absent means starcore.object, the root (§8.1.1)." }
    # §5.4.2's table declares `prop_def` as `combine = merge`: a second
    # block adds to the inherited property set rather than replacing it,
    # which is the only reading under which `class_extension` can add a
    # property to a class somebody else declared.
    key = { name = prop_def  type = block<prop_def>  arity = many  combine = merge
            doc  = "Properties this class declares, each a type or a block of markers." }
    key = { name = traits    type = list<identifier>
            doc  = "Traits mixed in, inherited by every instance and subclass." }
    key = { name = resolve   type = block<resolve>
            doc  = "Maps each contested property to the trait it comes from (§8.3)." }
    key = { name = sealed    type = bool
            doc  = "Redefinition is an error naming the owner (§7.2.2)." }
    key = { name = doc       type = text }
}

schema = {
    id        = class_extension
    top_level = yes
    sealed    = yes
    open      = yes  # as with `class`, any other key changes a default
    doc       = "Adds properties and changes defaults on a class declared elsewhere (§8.2)."

    key = { name = of_class  type = identifier       required = yes
            doc  = "The class being extended. It must already exist." }
    key = { name = prop_def  type = block<prop_def>  arity = many  combine = merge }
    key = { name = traits    type = list<identifier> }
    key = { name = doc       type = text }
}

schema = {
    id        = schema_extension
    top_level = yes
    sealed    = yes
    doc       = "Adds keys to a form declared elsewhere, including a sealed one (§7.5)."

    key = { name = of_schema  type = identifier   required = yes
            doc  = "The form being extended. It must already exist." }
    key = { name = key        type = block<key>   arity = many
            doc  = "Keys to add. A key that already exists is a redefinition, not an extension." }
    key = { name = doc        type = text }
}

schema = {
    id        = trait
    top_level = yes
    sealed    = yes
    open      = yes  # as with `class`, any other key sets a default
    doc       = "A named bundle of properties, defaults and rules that cuts across the class tree."

    key = { name = id        type = identifier       required = yes  unique_in = trait }
    key = { name = prop_def  type = block<prop_def>  arity = many  combine = merge }
    key = { name = rule      type = block<rule>      arity = many }
    key = { name = sealed    type = bool }
    key = { name = doc       type = text }
}

schema = {
    id        = enum
    top_level = yes
    sealed    = yes
    doc       = "A closed set of named values, usable as a type (§6.2)."

    key = { name = id      type = identifier        required = yes  unique_in = enum }
    key = { name = values  type = list<identifier>  required = yes }
    key = { name = doc     type = text }
}


# --- Save-state layout -------------------------------------------------------

schema = {
    id        = global
    top_level = yes
    sealed    = yes
    doc       = "A named mutable value in the save state (§6.4)."

    key = { name = id       type = identifier  required = yes  unique_in = global }
    key = { name = type     type = type_expr   required = yes }
    key = { name = default  type = scalar }
    key = { name = doc      type = text }
}

schema = {
    id        = const
    top_level = yes
    sealed    = yes
    doc       = "A named value fixed at compile time (§6.4)."

    key = { name = id     type = identifier  required = yes  unique_in = global }
    key = { name = type   type = type_expr   required = yes }
    key = { name = value  type = scalar      required = yes }
    key = { name = doc    type = text }
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

schema = {
    id        = library
    top_level = yes
    sealed    = yes
    doc       = "A library's own manifest (§13.3)."

    key = { name = id                   type = identifier        required = yes  unique_in = library }
    key = { name = version              type = string }
    key = { name = display_name         type = text }
    key = { name = requires             type = block<version_constraints> }
    key = { name = uses_editor_feature  type = list<identifier> }
    key = { name = provides_schema      type = list<identifier>
            doc  = "Forms this library contributes keys to, or declares (§7.1)." }
    key = { name = doc                  type = text }
}


# --- What core depends on ----------------------------------------------------

schema = {
    id        = core_requirement
    top_level = yes
    sealed    = yes
    doc       = "Something starcore depends on, stated so it can be checked (§7.2.2)."

    key = { name = id       type = identifier  required = yes  unique_in = core_requirement
            doc  = "What a failure is reported as. Every requirement has a name." }
    key = { name = requires type = identifier  required = yes
            doc  = "form, class, trait, property or parent." }
    key = { name = subject  type = identifier  required = yes
            doc  = "The form, class or trait the requirement is about." }
    key = { name = member   type = identifier
            doc  = "For 'property', the property name; for 'parent', the parent class." }
    key = { name = type     type = type_expr
            doc  = "For 'property', the type core reads that property at." }
    key = { name = doc      type = text
            doc  = "Why core needs this. Shown alongside the failure." }
}


# --- Nested block shapes -----------------------------------------------------

schema = {
    id     = prop_def
    sealed = yes
    open   = yes  # a map from property name to type, not a fixed set of keys
    doc    = "Property declarations: each key is a property name (§8.1, §7.2.3)."
}

# The marker vocabulary of §7.2.3, written down rather than hard-coded, so
# that core depends on a declared and checkable set. The engine never learns
# that a property called `open` affects scope; it learns that some properties
# do and is told which, which is what lets a ruleset with a `shuttered`
# property get the same behaviour for free.
#
# An unknown marker is refused by the ordinary closed-schema check. Adding a
# marker is an edit to this block and a line in starcore that acts on it.
schema = {
    id     = prop_marker
    sealed = yes
    doc    = "The markers a property may carry (§7.2.3)."

    key = { name = type             type = type_expr  required = yes
            doc  = "The property's declared type, as in the bare form." }
    key = { name = affects_scope    type = bool
            doc  = "Invalidate the scope cache when this property changes (proposal §5.4)." }
    key = { name = always_resident  type = bool
            doc  = "Never streamed out with its sector (proposal §5.3)." }
    key = { name = save_exclude     type = bool
            doc  = "Derived state; not written to the save file." }
}

schema = {
    id     = resolve
    sealed = yes
    open   = yes  # a map from contested property name to the trait it comes from
    doc    = "Resolves a trait conflict explicitly, per §8.3."
}

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

schema = {
    id     = version_constraints
    sealed = yes
    # Each key is a library id and each value a version comparison, so the key
    # set is whatever libraries exist rather than anything core can list.
    open   = yes
    doc    = "A library's version requirements, one per library id (§13.3)."
}
