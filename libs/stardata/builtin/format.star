# =============================================================================
#  stardata format schemas — the forms the format layer parses itself
# =============================================================================
#
#  Spec §7.2.4's second category. A form belongs here when `libs/stardata`
#  parses it with a reader of its own rather than validating it against a
#  schema like any other form — which is exactly the set whose shape would
#  otherwise be stated twice, in two libraries, with nothing checking that the
#  two agree. That is not hypothetical: `class` was declared in
#  `libs/starcore/builtin/schema.star` and read by `read_class`, and the two
#  drifted for long enough that `traits` was in one and not the other.
#
#  So the rule is mechanical, and `tests/unit/schema/builtin_test.cpp` enforces
#  it: every form here has a reader, every reader has a form here, and the keys
#  match. A form that grows a reader and is not moved here fails that test.
#
#  WHAT THIS IS NOT. It is not "the forms core does not care about". The engine
#  reads globals out of the save state and walks the class graph every turn;
#  what it does not do is *parse the declarations*, and §7.2.4 now asks those
#  as two separate questions. `core_requirement` is the case that makes the
#  distinction visible: §7.2.5.1 reserves *writing* one to `starcore`, and that
#  is enforced by a load flag rather than by where the form is declared. The
#  format layer parses it, checks it, and refuses it from anyone but core.
#
#  The declarations stay in data rather than moving into the C++ bootstrap so
#  that §7.1's other two purposes survive: `doc` strings reach diagnostics, and
#  an editor has something to walk. The two forms that could not stay —
#  `schema` and `key` — are in libs/stardata/src/schema/schema.cpp, because
#  they are what reads this file.
#
#  Owned by `stardata` and sealed. `stardata` is not a library id, so no
#  `@replaces(stardata)` can claim these (§7.6), which is the same protection
#  `starcore` has.
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
            doc  = "The parent class. Absent means the declared root (§8.1.1)." }
    key = { name = root      type = bool
            doc  = "This class is the root a parentless declaration descends from (§8.1.1). At most one class in a program may declare it." }
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
#
#  §6.4. Mutable saved state and immutable tuning values. The engine owns what
#  these MEAN at run time; the format layer owns reading the declaration, and
#  the two are different jobs.
#
#  `initial` and `value` carry `type_of = type` rather than a type of their
#  own: §7.2's dependent type, whose meaning is "checked against the value of
#  the sibling key named here". A global's starting value is an `int` for one
#  global and a `map<direction, ref<room>>` for the next, so no fixed `type =`
#  on those lines could be right for both.
#
#  The escape hatch a previous draft introduced instead was a type called
#  `any`, which said "somebody else checks this" — and said it to every schema
#  in every library, not just to this one key.

schema = {
    id        = global
    top_level = yes
    sealed    = yes
    doc       = "A named mutable value in the save state (§6.4)."

    key = { name = id       type = identifier  required = yes  unique_in = global }
    key = { name = type     type = type_expr   required = yes }
    key = { name = initial  type_of = type }
    key = { name = doc      type = text }
}

schema = {
    id        = const
    top_level = yes
    sealed    = yes
    doc       = "A named value fixed at compile time (§6.4)."

    key = { name = id     type = identifier  required = yes  unique_in = global }
    key = { name = type   type = type_expr   required = yes }
    key = { name = value  type_of = type     required = yes }
    key = { name = doc    type = text }
}


# --- Load order --------------------------------------------------------------
#
#  §13.3's manifest. What a library says it contributes, checked against what
#  it actually declared. `project` is NOT here: it is the game's manifest and
#  its contents (`start_room`, `player`, `entry_sector`) are core vocabulary.
#  Library is packaging; project is the game.

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

schema = {
    id     = version_constraints
    sealed = yes
    # Each key is a library id and each value a version comparison, so the key
    # set is whatever libraries exist rather than anything core can list.
    open   = yes
    doc    = "A library's version requirements, one per library id (§13.3)."
}
