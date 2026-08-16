# starcore

World model, action pipeline, parser host, scripting host (`docs/proposal.md`
§2.1). The C++ arrives in Phase 1.

## `builtin/` — here in Phase 0

What exists now is data, not code: the core-owned schema set of spec §7.2.2
and §7.2.4, written as ordinary Stardata.

| File | Holds |
|---|---|
| `builtin/schema.star` | The forms `starcore` reads and writes itself — `class`, `trait`, `enum`, `global`, `const`, `action`, `rule`, `turn_hook`, `sector`, `project`, `library` — plus `core_requirement`. Every one is `sealed = yes`. |
| `builtin/object.star` | `starcore.object` and its six slots (§8.1.1), `starcore.room`, and the `starcore.actor` trait. |
| `builtin/requirements.star` | What core depends on, stated as data so a failure is a named diagnostic at load rather than a crash much later. |

These are **owned by `starcore` and sealed**: a library may add keys to a form
through `provides_schema` (§13.3) and properties to a core class through
`class_extension` (§8.2), but may not redefine, retype or remove. Spec §7.2.2
is worth reading for why — ADRIFT 5 requires a library to create the location
properties the system uses, and Inform 7 attaches special handling to the
eighth action declared. Both are engines depending on a convention while
pretending the library is free.

The schema for `schema` itself is **not** here. It is hard-coded in
`libs/stardata/src/schema/schema.cpp`, because it is what reads these files,
and it is kept as small as that bootstrap allows.

### Loading

Phase 0 loads these from disk, so the validator can be developed against
them: `schema::load_directory(...)` in `libs/stardata/include/stardata/schema/loader.hpp`.
**Phase 1 embeds them into the binary** via a CMake-generated string literal,
so they stay one source of truth, diffable, and impossible to ship without.

`tests/unit/schema/` exercises all of it: the set loads with no diagnostic,
every core-owned form is sealed, every requirement is met, and a library
attempting anything only core may do is refused by name.
