# STAR IF System — Implementation Proposal

**Status:** Draft v0.6 · **Date:** 2026-08-17 · **Author:** drafted from `main-design-notes.md`

> **v0.2 changes:** ordered/unordered block distinction dropped (§4.2); pinned sectors (§5.3); a time and calendar model (§5.6); the turn sequence rebuilt around an initiative-ordered actor loop with explicit ruleset hook points (§7.1); action redirection and dialogue entry as effects (§7.2); script-only rules and a corrected statement of the data/script relationship (§8.4); combat pacing resolved (§9.3); party members (§9.5); full offstage simulation as an option (§10.1); multi-party dialogue and its presentation (§11); debugger as an author's-kit plugin (§13.3); a distro-style trust model (§14.4); licensing (§14.5).
>
> **v0.6 changes:** removal of `?=`; specifying the boundary between `stardata` and `starcore`.
>
> **v0.5 changes:** globals, constants and collection state (spec §6.4–§6.5); flags reduced to declared boolean globals; properties declarable on a single object (spec §8.7); property access rules with static narrowing and an explicit runtime escape (spec §8.8); disambiguation's unselectable-object problem, detected at compile time (§6.4.1); dynamic object creation and general tables recorded as deliberate omissions (spec §1.4).
>
> **v0.4 changes:** objects present in several places at once — backdrops and two-sided doors — as a presence relation distinct from containment, replacing the teleport hack (§5.7); the language layer: lexicon, adaptive text, narrative voice, and a survey of what morphology libraries actually exist (§4.10).
>
> **v0.3 changes:** default NPC combat response replaces the output-suppression idea (§7.1); mutable initiative order specified (§7.1.1); party members pin their sectors (§9.5); one universal clock with local-time display (§5.6); barks and interjections confirmed distinct, with `joins` on interjections (§11.5); signing backend recommendation revised from OpenPGP to Ed25519, with the GnuPG licensing question answered (§14.4). **All substantive design questions are now closed**; §16.4 holds three remaining judgement calls, none blocking.

---

## 0. Reader's guide

This document proposes a concrete implementation of the STAR IF System. It is opinionated where the design notes left a choice open, and flags every such call as a **[DECISION]** so it can be overturned cheaply. Places where the notes were silent and I had to invent something are flagged **[OPEN]** and collected in §16.

Sections 1–3 are the executive summary and technology picks. Sections 4–8 are the core engine. Sections 9–12 are the authoring-facing subsystems. Sections 13–15 are tooling, packaging and delivery.

---

## 1. Goals and non-goals

### 1.1 What the system is for

A parser-based interactive fiction authoring system with two differentiators against the incumbents (Inform 7, TADS 3, ADRIFT 5, Dialog):

1. **Large worlds stay fast.** Turn cost should scale with what is *near the player*, not with the size of the game. Thousands of rooms and tens of thousands of objects should be a design choice, not a performance cliff.
2. **RPG mechanics are first-class but optional.** Stats, combat, equipment, quests and NPC schedules are supported by the core rather than bolted on by each author from scratch — but a pure puzzle game pays nothing for them.

Supporting goals, in rough priority order:

- **Keyboard-first authoring.** A complete game should be writable without touching the mouse.
- **Fast, stable, cross-platform tooling.** Starbase must not be Windows-only, must not be slow, and must not crash. These are stated as goals because they are the specific, concrete grievances that motivated the project (§1.4), and they are the kind of goal that quietly evaporates unless written down and measured.
- **Plain-text source of truth.** Everything the editor produces is human-readable, diffable, and hand-editable. Version control works.
- **A living world.** NPC behaviour and dialogue get real infrastructure, not an `ask/tell` topic table.
- **Broad runtime reach.** Desktop, browser, mobile, and Glk from one core.
- **Safe by default.** Downloading a stranger's game file must not be a security decision the player has to think about.

### 1.2 Explicit non-goals

- **Not a choice-based IF tool.** Twine, Ink and Inky own that space. (Dialogue is choice-based *inside* a parser game — see §11 — but the top-level interaction model is a parser.)
- **Not natural-language authoring.** Inform 7's prose syntax is a beautiful dead end for the kind of structured, editable, tool-manipulable data this system needs. STAR's source format is deliberately mechanical.
- **Not a general game engine.** No 3D, no real-time loop, no physics. Turn-based text with rich presentation.
- **Not dynamically extensible at run time.** No `new Thing()`. Every object that can exist is declared in the source; see spec §1.4.1 for why, and for the pooling pattern that covers the cases it appears to rule out.
- **Not attempting Z-machine/Glulx output.** Those VMs cannot host the memory model described here. Glk is supported as a *frontend* (§12.4), which gets us the interpreter ecosystem's accessibility story without the VM's constraints.

### 1.3 The primary user

**The primary user of this system is you.** That is not a limitation to apologise for; it is a design constraint that should be stated plainly and used, because it settles arguments. When a decision trades generality against being right for the games you intend to write, take the second. When a feature is defensible only as "someone might want this", cut it. Community adoption, if it comes, is a welcome side effect of building a tool that is genuinely good for one demanding user — that is roughly how Inform, TADS and Dialog each happened.

This has a practical consequence for §15.1: the argument for pulling community-facing work earlier is weaker than I originally pitched it, and the roadmap has been adjusted accordingly.

### 1.4 What we are reacting against

Named explicitly, because "don't be like X" is a testable requirement in a way that "be good" is not. The ADRIFT 5 editor is the closest thing to the intended shape of Starbase, and its problems are the ones to avoid:

| ADRIFT 5 problem | Starbase requirement |
|---|---|
| Windows-only | Windows, macOS and Linux, all first-class, from one codebase |
| Slow | Interaction latency budget: **under 16 ms** for any edit, navigation or inspector update on a 5,000-object project. Measured in CI. |
| Crash-prone | No unsaved-state loss, ever: the CST is the model, edits are journalled, and a crash resumes from the journal. Fuzz the parser and the edit operations. |
| Not keyboard-navigable | §13.2 — every command reachable from the palette, no mouse-only operation |

Inform 7's problems are different and also instructive: rule-ordering opacity (answered in §7.4), the impossibility of tool-manipulating the source (answered in §4 and §13.1), and turn cost that scales with game size (answered in §5).

### 1.5 Naming

| Component | Name | Notes |
|---|---|---|
| Editor / IDE | **Starbase** | From the notes. |
| Compiler / packager | **Starforge** | From the notes. |
| Runtime / interpreter | **Starhelm** | Confirmed. |
| Source data format | **Stardata** (`.star`) | The Clausewitz-derived format. |
| Compiled distributable | **Starpak** (`.spak`) | |
| Scripting language surface | **Starscript** | Lua 5.4 plus the STAR API. |
| First-party RPG ruleset and game series | **Starscape** | name reserved (§14.5). |

The system as a whole is the **STAR IF System**; third-party games are encouraged to carry **"Built on STAR IF technology"** (§14.5).

No trademark search — per your call, and the reasoning is sound: the IF community's tooling namespace is already a comedy of ungoogleable names, and a hobby project is not going to be litigated over. The one place this could bite is a package-manager or app-store name collision, which is cheap to check at the moment of publishing and not before.

---

## 2. Technology stack

**[DECISION]** Per your selection: **C++ with Qt** for the host, **Lua** for author scripting.

| Layer | Choice | Rationale |
|---|---|---|
| Language | **C++20** (C++23 where compiler support allows) | Concepts, ranges, `std::span`, designated initialisers and `constexpr` containers materially improve the data-model code. C++20 is the floor because Qt 6.5+ requires C++17 and the codebase benefits from concepts in the property-type system. |
| Build | **CMake 3.24+** with presets; **vcpkg** manifest for dependencies | Presets give one-command builds on all three desktop platforms and in CI. |
| GUI | **Qt 6.6+ (Widgets)** for Starbase; **Qt Quick** only for the runtime's rich frontend | Widgets for the editor because keyboard-first, dense, tree/table-heavy tooling is what Widgets is good at and Quick is not. Quick for the game window because that is where animation, media and styled text flow matter. |
| Scripting | **Lua 5.4** + **sol3** bindings | Small, embeddable, sandboxable, familiar to modders. See §8 for why *not* LuaJIT. |
| Serialisation | Hand-written parser for Stardata; **CBOR** (via a vendored minimal writer) for the binary object table and save deltas | CBOR gives us self-describing binary without a schema compiler, and has a trivial reader for the WASM target. |
| VFS | **Custom layered VFS** modelled on PhysicsFS, not PhysicsFS itself | We need write-through layering, in-memory layers, and mount-order queries that PhysicsFS's API does not expose cleanly. ~1.5k lines. Backed by **libzip** or **miniz** for archive layers. |
| Text shaping | Qt's own (HarfBuzz) on desktop/mobile; browser native on web; plain on Glk | |
| Testing | **Catch2** for unit tests; a bespoke **transcript harness** for game-level tests (§13.3) | |
| Web target | **Emscripten** for the core; TypeScript + DOM for the web frontend | The core compiles to WASM; the frontend is native web, talking the frontend protocol (§12.2) over a message channel. |

### 2.1 Repository layout

```
star-if-system/
  cmake/                 # toolchain files, presets, packaging scripts
  docs/                  # this document, format spec, author manual
  libs/
    starcore/            # world model, action pipeline, parser, scripting host
    stardata/            # Stardata lexer, CST, AST, loader, writer
    starforge/           # compiler: Stardata -> Starpak
    starvfs/             # layered virtual filesystem
    startext/            # text template VM, styling, output event stream
    starlang/            # language-neutral lexicon and form selection (§4.10)
    stardebug/           # debugger core — author's kit only (§13.3)
  apps/
    starbase/            # Qt Widgets editor
    starhelm-qt/         # Qt Quick desktop runtime
    starhelm-cli/        # headless runtime (testing, CI, piping)
    starhelm-glk/        # Glk frontend shim
    starhelm-web/        # Emscripten + TS frontend
    starforge-cli/       # command-line compiler
  stdlib/                # the standard library, written in Stardata + Lua
    core/                # kinds, actions, parser grammar, default messages
      lang/en/           # English rule table and enumerated action verbs
    starscape/           # the first-party RPG ruleset
  tests/
    unit/                # Catch2
    transcripts/         # game-level regression tests
    corpus/              # sample games, including the perf stress game
```

**Rationale for the split:** `starcore` knows nothing about Qt; `starforge` depends on both but on no UI. That means the compiler and the CLI runtime build and test without Qt at all, which keeps CI fast and makes the WASM build tractable. The `stardata`/`starcore` boundary is subtler and gets its own section.

### 2.1.1 The `stardata` / `starcore` line

An earlier draft said "`stardata` knows nothing about IF" and left it there. That was an assertion without a test, and it did not survive the first attempt to implement the schema layer: desugaring `in = ornate_box`, checking that a `restrictions` block carries a message, and narrowing a slot's type across an action's stages are all IF semantics, and all three were sitting in `stardata`.

**The line is mechanism versus vocabulary.**

> **`stardata` implements the format and the schema *mechanism*. `starcore` implements the *vocabulary* expressed in it.**
>
> `stardata` knows that forms have keys, that keys have types and arities, that schemas can be sealed and extended and replaced, that values must typecheck and references must resolve. It does not know what any particular form *means*.
>
> `starcore` knows what `class`, `action` and `rule` mean; what `holder` and `relation` are; that a `restrictions` block owes the player a message; and in what order an action's stages run.

This is the same relationship as JSON to a JSON-Schema-for-your-app, except that Stardata's mechanism is richer — sealing, extension, exclusive groups, markers — precisely so that more of the vocabulary can be *declared* rather than compiled in.

#### Two tests

**1. The deletion test.** Remove every `.star` file and every schema from the project. Does `libs/stardata` still compile, and is it still useful — can it parse, round-trip, edit, and validate against schemas supplied by someone else? If yes, the line holds.

**2. The grep test, which is mechanical and therefore worth asserting.** Does `libs/stardata` name a piece of the core *vocabulary* declared in `libs/starcore/builtin/` — a class or trait id, a property name, an enum id, an enum value? `starcore.object`, `holder`, `relation`, `present_in`, `relation_enum`, `carried`: if any of those is a string literal there, the layering has leaked.

**Not every identifier in `builtin/`**, and the exception used to be the whole difficulty of stating this rule. A schema layer forbidden from saying `key` could not read a schema, so the schema language has to be exempt — and the first version of this check carved that exemption out of `libs/starcore/builtin/` itself, by treating the id of a `schema` and the names of the keys it declared as mechanism. That was much weaker than it looked: `restrictions`, `failureMsg`, `when` and `effects` are keys of the core `action` and `rule` forms, so the carve-out exempted the four names this section uses as its own examples of vocabulary. Twenty names guarded, eighty-four exempt.

**The fix was to put the distinction in the data**, which is what spec §7.2.4's *format forms* are. The schema language now lives in its own directory, `libs/stardata/builtin/format.star`, and the rule becomes a question about where a name is declared rather than about its shape:

- `libs/stardata/builtin/` — the schema language. `class`, `trait`, `enum`, `global`, `of_class`, `prop_def`, `sealed`, `arity`, `type`. `stardata` implements these and so must name them.
- `libs/starcore/builtin/` — the vocabulary, **all of it**. Form ids and key names as well as class ids, property names and enum values. `stardata` may name none of them.

A name in both sets counts as schema language: `name` is a property of `starcore.object` and also a field of a `key` declaration, and no arrangement of the code would let the schema layer stop saying it. Plus two sets read out of the C++, for what the format defines in code rather than in data — §5.4.1's annotations and §6.2's type names — which is what makes `style` and `duration` usable: each is one spelling for two things, one of them the format's own grammar.

This is a CI check, `scripts/check_layering.py`, for the same reason §7.2.2 of the spec insists core assert rather than hope: a boundary nobody verifies is a boundary that erodes. Every set is derived from the files rather than listed in the script, so the guarded set grows with the object model. It found exactly two leaks when it was written — `holder` and `relation`, in the placement pass this section sent to `starcore`. **Fifty-four names are guarded now**, `restrictions`, `failureMsg`, `when` and `effects` among them, which closes the `[OPEN]` this paragraph used to carry.

A second check joins it, `scripts/check_format_forms.py`, because moving the schema language into data creates a new way to be wrong: a format form is now stated twice, as a declaration and as the reader that parses it. That had already gone wrong once — `class` declared a `traits` key nothing read, for nine tasks — so the two statements are checked against each other. See backlog F13.

#### Where the current Phase 0 tasks land

| Task | Owner | Note |
|---|---|---|
| F1 typed AST view | `stardata` | the shape of a tree, not what it means |
| F2 registry, sealing mechanism | `stardata` | |
| F2 the core-owned *content* — `starcore.object`, the forms | `starcore` | it **is** the vocabulary |
| F2a `core_requirement` | `stardata` (F13) | the *form* is the format's — it is the gate that runs before core sees anything. Writing one stays reserved to `starcore` (spec §7.2.5.1), which is a separate axis |
| F2b marker mechanism / marker names | `stardata` / `starcore` | `prop_def` may carry markers; `affects_scope` is a scope concept |
| **F2c placement sugar** | **`starcore`** | `in`/`on`/`carried` are containment, and containment is IF |
| F2d `schema_extension`, `@replaces`, no-duplicates | `stardata` | pure registry rules |
| F3 key validation · F4 types · F5 combination · F6 suggestions | `stardata` | |
| F7 template *grammar* / template *builtins* | `stardata` / `starlang` | `[the noun]`'s syntax is generic; what `the` does is not |
| **F8 `failureMsg` placement** | **`starcore`** | entirely about the condition vocabulary |
| F9 reference resolution | `stardata` | "this id resolves" is generic |
| F10 globals mechanism / `flag_set` sugar | `stardata` / `starcore` | as landed it was all `starcore`; F13 moved the declaration half back |
| F11 object-local `prop_def` | `stardata` | |
| **F12 property access and narrowing** | **split — see below** | |
| F13 the boundary formalised | both | format forms named as a category, and the two CI checks that hold them |

#### F12, which is the interesting one

You are right that narrowing blows past any line drawn naively, because it needs to know that `when` precedes `conditions` precedes `restrictions` precedes `effects`. That ordering is the action pipeline (§7.1), which is unambiguously `starcore`.

But the *dataflow* is not IF-specific at all: "a predicate appearing earlier in a conjunction refines what later siblings may assume, and refinement flows forward through an ordered sequence of stages" is a generic analysis. It only looks IF-specific because the stage names are hard-coded.

So parameterise it. The schema declares its own stage order:

```stardata
schema = {
    id = action
    stage_order = { when conditions restrictions effects report }
}
```

`stardata` implements narrowing over *whatever* sequence a schema declares, and knows none of those five words. `starcore` supplies the sequence. This is exactly the markers-not-magic-names move of spec §7.2.3, applied one level up, and it turns the task that most threatened the boundary into the one that best demonstrates it.

The same trick does *not* rescue F2c or F8, and it should not be forced to. Placement sugar and `failureMsg` are vocabulary through and through; they belong in `starcore` and generalising them would produce a mechanism with exactly one user.

#### The practical consequence for Phase 0

**Create `libs/starcore` now**, even though it will be nearly empty until Phase 1, and put the vocabulary-aware passes there from the first commit. A target and a directory cost nothing; a semantic pass that lives in `stardata` "for now" is how the line stops being real. This also narrows Phase 0's exit criterion to `stardata`'s half of the work, which is welcome given the phase is already over its original estimate.

### 2.2 What is *not* in C++

The standard library (`stdlib/`) is written in Stardata and Lua, not C++. This is a deliberate constraint that forces the engine to expose enough power for authors to do what the stdlib does. If a stdlib feature needs a C++ hook, that is a signal the extension surface is too narrow, and the hook gets designed as a general mechanism rather than a special case. §9.1 discusses where this rule bends for the RPG layer.

**With one correction, because as first stated this was too absolute.** A handful of schemas describe data that `starcore` itself reads and writes — the containment tree, an object's class and sector, the action and rule forms the dispatch index is built from. Those are **core-owned**: registered from `starcore` before any file loads, sealed against redefinition, and asserted rather than assumed (spec §7.2.2). Every world object descends from a built-in root class, `starcore.object` (spec §8.1.1).

The reason is that the alternative is a documented failure mode rather than a hypothetical one. ADRIFT 5 needs a library to create the location properties its engine uses; Inform 7 attaches special handling to the eighth action declared, expecting Going. In both, an engine depends on a convention while presenting the library as free to choose — and the dependency is invisible in the source until it breaks. Stating the floor and checking it is strictly better than hoping.

The constraint that survives is the useful half: **`stdlib` gets no privileges the core doesn't grant every library.** `thing`, `person`, `container`, `door`, every action and every message are ordinary Stardata, replaceable wholesale.

**Recommended implementation.** Rather than hand-written C++ registration calls, keep the core definitions as Stardata in `libs/starcore/builtin/*.star` and **embed them into the binary at build time** via a CMake step that generates a string literal. One source of truth, readable and diffable, validated by the same tooling as everything else, and impossible to ship without. During Phase 0 the files are loaded from disk so the schema validator can be developed against them; the embedding step lands with Phase 1.

---

## 3. System overview

```
                    ┌───────────────────────────────────────┐
   author edits ──▶ │  Starbase (Qt Widgets editor)         │
                    │  projectional views over a lossless   │
                    │  CST; keyboard-first command surface  │
                    └────────────────┬──────────────────────┘
                                     │ reads/writes
                                     ▼
                    ┌───────────────────────────────────────┐
                    │  Stardata source tree (.star + .lua)  │
                    │  the single source of truth           │
                    └────────────────┬──────────────────────┘
                                     │
                       ┌─────────────┴──────────────┐
                       │                            │
              (dev: load directly)          (dist: compile)
                       │                            ▼
                       │            ┌───────────────────────────────┐
                       │            │  Starforge                    │
                       │            │  resolve, typecheck, index,   │
                       │            │  lower templates, pack        │
                       │            └───────────────┬───────────────┘
                       │                            ▼
                       │            ┌───────────────────────────────┐
                       │            │  game.spak (VFS archive)      │
                       │            └───────────────┬───────────────┘
                       │                            │
                       └────────────┬───────────────┘
                                    ▼
              ┌──────────────────────────────────────────────┐
              │  Starhelm core                               │
              │  ┌────────────────────────────────────────┐  │
              │  │ VFS (base < dlc < mods < save)         │  │
              │  ├────────────────────────────────────────┤  │
              │  │ World store: objects, sectors, index   │  │
              │  ├────────────────────────────────────────┤  │
              │  │ Parser → Action pipeline → Rules       │  │
              │  ├────────────────────────────────────────┤  │
              │  │ Lua sandbox (Starscript)               │  │
              │  ├────────────────────────────────────────┤  │
              │  │ Text template VM → output event stream │  │
              │  └────────────────────────────────────────┘  │
              └───────────────────┬──────────────────────────┘
                                  │ frontend protocol (§12.2)
        ┌────────────┬────────────┼────────────┬─────────────┐
        ▼            ▼            ▼            ▼             ▼
   Qt Quick       Web (TS)      Glk         CLI          Mobile
   (rich)         (rich)        (plain)     (test)       (Qt Quick)
```

Two load paths matter:

- **Development path.** Starbase and `starhelm-cli` can load a source tree directly, no compile step. Edit-to-play latency is the single most important number for author happiness, and we should treat any regression in it as a bug. Target: **under 400 ms** from keystroke to playable for a 5,000-object project.
- **Distribution path.** Starforge produces a `.spak`. Loading it is a mostly-mmap-and-fixup operation with no parsing.

---

## 4. The Stardata format

### 4.1 Design principles

The Clausewitz format was chosen (per the notes) because it is:

- trivially machine-editable — the editor can rewrite a block without a full re-serialise;
- trivially diffable and mergeable;
- hospitable to *extension* — `class_extension` in your example is the key move, and it generalises;
- terse enough that hand-writing a room takes four lines.

Two things need fixing relative to raw Clausewitz, which is famously under-specified:

1. **A real grammar.** Clausewitz's parser is permissive to the point of ambiguity; ours needs to be strict enough that an editor can round-trip it and error messages can point at exact spans.
2. **Types.** Clausewitz is untyped; a project with 30,000 objects needs the compiler to catch `condition = breathble` at build time, not at play time.

### 4.2 Lexical structure

```
Comment      ::= '#' <any chars to end of line>
Identifier   ::= [A-Za-z_][A-Za-z0-9_.]*
Integer      ::= '-'? [0-9]+
Decimal      ::= '-'? [0-9]+ '.' [0-9]+
String       ::= '"' ( <char except " and \> | '\' <escape> )* '"'
LocKey       ::= '$' Identifier            # localisation key reference
Annotation   ::= '@' Identifier
Operator     ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-='
Punctuation  ::= '{' | '}' | '[' | ']'
```

Whitespace, including newlines, is insignificant except as a token separator. Newline-separated and space-separated lists are identical.

Additions to your example's syntax, each justified:

- **`==` and comparison operators.** Your `restrictions` blocks need to express `strength > 12`. Using bare `=` for both assignment and comparison is Clausewitz's original sin and produces genuinely confusing scripts. Stardata uses `=` for assignment/binding only, and `== != < > <= >=` in condition contexts. In a condition context, a bare `=` is a *warning* that suggests `==`, not an error, so the format stays forgiving for beginners. **[DECISION]**
- **`+=` / `-=`.** For extending inherited list-valued properties without restating them, e.g. `synonyms += { lantern lamp }`.
- ~~**`?=`** — "set only if not already set".~~ **Removed**; see spec §6.3.1. Whether it bound could not be determined locally, and load order already gave a library's default the pre-emptability it was meant to provide.
- **`$key`** for localisation references (§4.8).

**Everything is ordered.** v0.1 proposed a `{ }` (unordered) versus `[ ]` (ordered) distinction. Dropped, on your objection, which I think is correct on all three counts:

1. The blocks where order is *obviously* meaningful (effects, dialogue nodes, quest stages, schedule entries) turn out to outnumber the ones where it isn't.
2. The ones where it isn't — enum values, rule conditions, trait lists — lose nothing by being ordered anyway.
3. §5.5 requires deterministic iteration order for *everything* regardless. Having a syntactic category that claims to be unordered while the engine guarantees a fixed order is a distinction that exists only to be violated.

Two further benefits I hadn't weighed: it keeps `[` and `]` reserved exclusively for string interpolation (`[the noun]`) and parser grammar tokens (`[something]`), so those never visually collide with block syntax; and it removes a whole category of "should this be braces or brackets?" from the author's head. The grammar in §4.3 is correspondingly simpler.

The one thing given up is the editor's licence to silently re-sort a block. That becomes an explicit, author-invoked command ("sort this block alphabetically") that produces a visible diff, which is better behaviour anyway. **[DECISION]**

### 4.3 Grammar

```ebnf
File        ::= Statement*
Statement   ::= Key Op Value
Key         ::= Identifier | String
Op          ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-='
Value       ::= Annotation* ( Scalar | Block )
Scalar      ::= Identifier | Integer | Decimal | String | LocKey | 'yes' | 'no' | 'inherit'
Block       ::= '{' ( Statement | Scalar )* '}'
```

That is the whole grammar. A `Block` may contain bare scalars (making it a list: `values = { breathable toxic }`) or statements (making it a record: `exits = { north = corridor }`), but **not both**. Mixing them is an error with a fix-it suggestion; this removes the main source of Clausewitz parse ambiguity.

**Block contents are ordered, always.** The loader preserves source order, the compiler preserves it into the `.spak`, and every engine API that iterates a block iterates in that order. Where order carries no semantics (an enum's values), nothing is lost; where it does (an effects list, a dialogue's nodes), it is guaranteed rather than incidental.

Duplicate keys within a block are permitted and meaningful for keys declared as *multi* in the schema (`NOT = { ... } NOT = { ... }` in your example, and `prop_def`, `rule`, `exits`, `node`, `stage`, `choice` entries). Repeated keys retain their relative order. For non-multi keys, a duplicate is an error citing both spans.

### 4.4 The schema layer

Every top-level statement kind (`enum`, `class`, `class_extension`, `action`, `rule`, `room`, …) is described by a **schema**: which child keys are allowed, their types, arity, and whether they are multi. Schemas are themselves declared in Stardata, in `stdlib/stdlib/schema.star`, so libraries can add new top-level forms:

```stardata
schema = {
    id = action
    doc = $schema_action_doc
    key = { name = id          type = identifier  required = yes  unique_in = action }
    key = { name = match       type = list<string> required = yes }
    key = { name = restrictions type = condition_block }
    key = { name = effects     type = effect_block }
    key = { name = successMsg  type = text_or_script }
    key = { name = check_every type = enum<turn_phase> default = action_phase }
}
```

This buys three things at once:

- **Compile-time validation** with precise errors.
- **Editor UI generation.** Starbase renders an inspector for any object by walking its schema. A library that adds `schema = { id = faction ... }` gets a first-class editor form for free, with no editor changes. This is the mechanism that makes the RPG layer's "special kinds appear in the editor" (notes §12) work generically rather than as a hard-coded special case.
- **Documentation and autocomplete** from one source.

### 4.5 Objects, classes and properties

The model is **prototype-based with class-level defaults**, which is what your example already implies:

- A **class** declares properties (`prop_def`) and default values, and inherits from one parent class (`of_class`). Single inheritance, plus **traits** for cross-cutting behaviour (§4.6).
- An **object** is an instance of a class. `room = { id = your_cell ... }` creates an object of class `room`.
- A property lookup on an object walks: object's own slots → class chain → traits (in declaration order) → error.

On your parenthetical in the example ("I'm not sure if it's more convenient to place the class name or the new object's ID on the left side"): **class name on the left is right**, and should stay. It makes the file scannable by kind, it lets the editor's outline group by type without an index, and — decisively — it lets the schema layer dispatch on the left-hand key. The `id =` inside is a small price. **[DECISION]**

Property types available to `prop_def`:

| Type | Notes |
|---|---|
| `bool` | `yes` / `no` |
| `int`, `decimal` | `decimal` is fixed-point (3 fractional digits) not float — see §5.5 on determinism |
| `text` | A localisable, interpolatable string |
| `identifier` | Bare symbol |
| `ref<Class>` | A reference to an object of `Class` or a subclass; validated at compile time |
| `enum<E>` | A value from a declared `enum` |
| `list<T>`, `set<T>` | Collections. Both iterate in declaration order (§4.3); `set` additionally enforces uniqueness and gives O(1) membership. |
| `map<K,V>` | e.g. `exits` is `map<direction, ref<room>>`; iterates in declaration order |
| `script` | A named Lua function |
| `flags` | Bitset over a declared enum; cheap to test |

### 4.6 Traits

Single inheritance is not enough. `container` and `supporter` are classes, but "openable", "lockable", "lit", "wearable" and "edible" are orthogonal capability bundles that any class may need. Inform solves this with "either/or properties" plus rule preambles; TADS with multiple inheritance (and the resulting MRO headaches).

Proposal: **traits** — named bundles of properties, defaults, and rules that classes and individual objects can mix in:

```stardata
trait = {
    id = openable
    prop_def = {
        open = bool
        openable_by_hand = bool
    }
    open = no
    openable_by_hand = yes
    # traits may carry rules; they are attached to whatever mixes the trait in
    rule = {
        of_action = open
        conditions = { noun = { has_trait = openable } }
        restrictions = {
            noun = { open == no  failureMsg = $already_open }
        }
        effects = { set = { target = noun  prop = open  value = yes } }
        successMsg = $opened_default
    }
}

class = {
    id = door
    of_class = thing
    traits = { openable lockable }
}

# individual objects can mix in traits too
thing = {
    id = ornate_box
    of_class = container
    traits = { openable lockable }
    lock_key = brass_key
}
```

Trait conflict resolution: if two traits define the same property, that is a compile error unless the mixing class explicitly resolves it (`resolve = { open = from openable }`). No implicit MRO, no silent surprises. **[DECISION]**

### 4.7 The `@` annotations

Your rule-phase annotations (`@before`, `@after`, `@override`) generalise to any block or scalar value. The full set proposed:

| Annotation | Meaning |
|---|---|
| `@before` | This block's contents run/apply before the inherited ones |
| `@after` | …after |
| `@override` | …instead of |
| `@merge` | Merge into the inherited block key-by-key (default for `prop_def`) |
| `@remove` | Remove the named entries from the inherited collection |
| `@priority(n)` | Explicit ordering tiebreak within a phase; default 0, higher runs first |
| `@debug` | Contents apply only in development builds; Starforge strips them |
| `@platform(...)` | Contents apply only on listed frontends, e.g. `@platform(glk)` for a text-only fallback |

The defaults you specified in the example are kept exactly: empty block → `@override`; non-empty `restrictions` → `@after`; `effects` → `@override`; `successMsg` → `@override`. These defaults are surprising in isolation but correct in practice, so Starbase's rule editor should show the *effective* phase as a visible dropdown rather than leaving it implicit. `@debug` and `@platform` are new.

### 4.8 Text, localisation and interpolation

Author-facing text lives in one of two places:

**Inline**, for anything short and unambiguous:
```stardata
successMsg = "You are already holding [the noun]."
```

**In a locale file**, referenced by `$key`, for anything a translator should see:
```stardata
# stdlib/stdlib/loc/en.star
loc = {
    lang = en
    already_holding = "You are already holding [the noun]."
    opened_default   = "You open [the noun]."
}
```

Inline strings are implicitly assigned generated keys at compile time, so a game can be localised after the fact without the author having restructured anything. Starbase offers "extract to locale file" as a one-key refactor.

**Interpolation syntax:** `[expr]`, where `expr` is a call to a Starscript function or a bare property path (`[noun.name]`). Literal `[` is `\[`.

**Compilation of templates.** Per the notes, Starforge lowers each interpolated string into a program for a small **text VM** rather than into a Lua closure. Opcodes:

```
EMIT_LIT <string-id>        push literal span
EMIT_CALL <fn-id> <argc>    call a Starscript or builtin function, emit result
PUSH_REF <slot>             noun / second / actor / self
PUSH_PROP <slot> <prop-id>
BR_IF_FALSE <offset>        for conditional fragments
STYLE_PUSH <style-id>       styling (§12.3)
STYLE_POP
TOOLTIP_BEGIN <obj-slot>    mark span as hoverable (§12.3)
TOOLTIP_END
PLURAL <n-slot>             select from a plural table
```

Why a dedicated VM rather than Lua closures: the overwhelmingly common case (`"You take [the noun]."`) then never crosses the Lua boundary at all. Measured on comparable systems, Lua call overhead dominates text generation in message-heavy turns. Functions the VM does not know are dispatched to Lua as a fallback, so authors lose nothing.

### 4.9 The living format specification

Per the notes, the example file is a spec that must not rot. Proposal:

- `docs/stardata-spec.md` holds the normative grammar and semantics. **[Written]**
- `star-if-example.txt` is promoted to `tests/corpus/tour.star` — a file that exercises every construct in the spec **and is parsed as part of CI**. If the spec and the parser diverge, the build breaks. **[Written]**
- `tests/corpus/invalid/` holds negative fixtures, one per rule that can be violated, each declaring the diagnostic code it must provoke. A rule with no fixture is one nobody has confirmed can fail. **[Written]**
- `tests/check_stardata.py` enforces all of the above from day one, before any engine code exists. It is explicitly temporary: when Starforge can validate the corpus, the script is deleted rather than maintained in parallel. **[Written]**
- A CI step diffs the grammar in the spec against the parser's grammar table and fails on mismatch. **[Pending — needs the real parser]**

As a small illustration of why the schema layer earns its keep: the current `star-if-example.txt` declares `class = { id = outdoors_room }` and then instantiates `outdoor_room = { id = antecourt }`. Under §4.4 that is a compile error pointing at the exact span with a "did you mean `outdoors_room`?" suggestion. In an untyped Clausewitz-style loader it is a silently ignored block and a room that mysteriously doesn't exist.

### 4.10 Adaptive text and the language layer

Templates (§4.8) are not enough on their own. "You take the lamp" and "Vex takes the lamp" are the same message with a different actor, and no amount of string interpolation makes `take`/`takes` come out right without the engine knowing something about English.

#### 4.10.1 The problem is much smaller than Inform's trie suggests

That excerpt is worth reading closely, because it explains its own size. `boob 0ing`, `squid 0ding`, `whid 0ding`, `stravaig 0ing` — those are present-participle rules for words that will never be a verb in a text adventure. They exist because Inform makes an **open-class promise**: any English word may be inflected anywhere. Honouring that promise requires a lexicographer, and Graham Nelson evidently found one.

We do not have to make that promise, and two structural facts let us out of it.

**Verbs in IF are very nearly closed-class.** The verbs that need inflecting are overwhelmingly the actions — take, drop, open, examine, attack — and the action set is enumerated in the game's own source. There are perhaps fifty. Writing out their forms is five strings apiece, correct by construction rather than by rule, and localises by translation rather than by re-implementation:

```stardata
action = {
    id   = take
    verb = { base = "take"  third = "takes"  past = "took"
             past_participle = "taken"  present_participle = "taking" }
}
```

Fifty actions × five forms is 250 strings, written once, in the standard library. Compare with two thousand lines of trie whose purpose is to derive them.

**The open-class residue is enumerable at compile time.** Authors do write custom messages with verbs outside the action set — "Vex polishes the console." But that verb appears inside a template, templates are compiled (§4.8), and so **Starforge can enumerate every word in the game that needs a form it does not have.** The same holds for noun plurals.

That is the move that makes this tractable. It turns an unbounded linguistic problem into a bounded, reviewable list:

1. The language pack's rules infer the missing forms.
2. Starforge reports **every inferred form** in its build summary.
3. The author reads a list of perhaps fifty guesses once, fixes the three that are wrong, and never thinks about it again.
4. The corrections are data — diffable, translatable, reviewable.

Inform's trie is enormous because Inform cannot ask the author. We have a compiler and an editor, so we can. Starbase should surface this as a **"review inferred forms"** panel, which is also the natural place to notice that you have accidentally called something a "sheeps".

#### 4.10.2 The lexicon

The core knows nothing about English. It knows that a word has forms, that forms are selected by grammatical features, and that a language pack supplies both the feature set and the rules for inferring missing entries.

```stardata
lexicon = {
    lang = en

    # Only what the rules get wrong. Everything else is inferred and reported.
    noun = { base = "knife"   plural = "knives" }
    noun = { base = "sheep"   plural = "sheep" }
    noun = { base = "hour"    article = "an" }     # phonetic, not orthographic
    verb = { base = "polish"  third = "polishes" }
}
```

Template substitutions, all resolving through the lexicon:

| Substitution | Produces |
|---|---|
| `[the noun]` · `[a noun]` · `[name noun]` | article and name; `a` picks *a*/*an* phonetically from the lexicon |
| `[verb(actor, take)]` | agrees with the actor's person and number |
| `[is actor]` · `[has actor]` | irregular auxiliaries, tabulated rather than derived |
| `[plural noun]` · `[number n]` | |
| `[pronoun actor]` · `[possessive actor]` · `[reflexive actor]` | |

Two spelling rules make this readable, and both are general rather than special cases for articles. **Juxtaposition is single-argument application** — `[the noun]` is exactly `[the(noun)]`, and either spelling is legal for any one-argument function; anything taking two arguments, such as `[verb(actor, take)]`, uses parentheses. **A capitalised name capitalises its result** — `[The actor]` resolves `the` and capitalises, implemented once generically rather than as a twin for every builtin. Spec §9.2.1–§9.2.2.

```stardata
successMsg = "[The actor] [verb(actor, take)] [the noun]."
# player -> "You take the brass key."
# NPC    -> "Vex takes the brass key."
```

**Narrative voice becomes a project setting**, almost free once forms are tabulated rather than derived:

```stardata
defaults = {
    narrative_person = second      # first | second | third
    narrative_tense  = present     # present | past
}
```

A game written in the past tense costs one declaration instead of rewriting every message.

#### 4.10.3 Why generation is the wrong abstraction for localisation

This is the argument that should decide the design, and it is worth settling before any library is chosen.

An English morphology *generator* does nothing for German (three genders, four cases), Russian (six cases), or Finnish (fifteen). If the architecture is "derive forms from rules", every new language needs a new derivation engine and the localisation phase becomes a rewrite. If the architecture is "the author or translator supplies forms; the engine selects among them by grammatical feature", a new language is a new table and a new selector — which is exactly what ICU MessageFormat and CLDR do, and they do it that way for precisely this reason.

So: **rules are a convenience for authors writing in the source language, never the mechanism.** The mechanism is selection over supplied forms. The English pack's rule table exists to save typing, and its output is always reviewable and overridable. This also makes the compile-time report of §4.10.1 structural rather than a nicety — it is the seam that stops inference from quietly becoming load-bearing.

#### 4.10.4 Libraries — what actually exists

I looked rather than guessed. There is one serious candidate and it is not yet usable for us.

**[Unicode Inflection](https://github.com/unicode-org/inflection)** is the right long-term answer. A C/C++ library from the Unicode Consortium, donated by Apple's Siri team, built for exactly this problem: inflecting a word into another surface form, and grammatical agreement between words, across many languages. It is C++20, matching our stack, and under the permissive Unicode License, which is Apache-2.0-compatible. Its documentation was regenerated in July 2026, so it is live.

The problems are practical rather than conceptual:

- **No Windows support.** Supported platforms are the Apple OSes plus UBI Linux 9 and Ubuntu 22; the project invites Windows contributions. Starbase must run on Windows (§1.4), so this alone is disqualifying today.
- **No tagged releases**; 33 stars, 102 commits, 22 open issues. Early.
- **Heavy dependencies**: ICU4C, marisa-trie, and memory-mapped lexical dictionaries shipped via Git LFS. Acceptable for a desktop editor; a great deal of weight in a WASM runtime (§12.5) or a mobile build for the sake of `take` → `takes`.
- **UTF-16 API**, where we would be UTF-8 throughout.

**Verdict:** make the lexicon's backend pluggable, ship a rule table now, and re-evaluate in Phase 7 when localisation actually starts. By then it may have Windows support and releases, and it is much the best thing to inherit if it does.

The others, briefly:

| Option | Assessment |
|---|---|
| **`inflect`** (Python, MIT) | The most complete practical English inflection ruleset in any language. MIT means we can **port its tables** — the pragmatic answer for the English pack's data. Write no linguistics ourselves. |
| **`en-inflectors`** (JS) | Smaller; covers nouns, verbs and adjectives. A useful cross-check against `inflect`. |
| **ICU `PluralRules`** | Not an inflector. Gives CLDR plural *categories* (one/other/few/many) per language, for **selecting** among author-supplied forms — exactly the §4.10.3 primitive. Worth taking when we localise, as an optional dependency. |
| **Hunspell** | Has a generation API, but it is a spell checker; English dictionaries are not built for generation and results depend on affix flags never intended for it. Wrong tool, and the GPL/LGPL/MPL tri-licence adds friction for no gain. |
| **SimpleNLG** | The classic surface realiser and conceptually the closest fit — but Java. A JNI dependency is incompatible with shipping Glk, WASM and mobile builds. Out. |

**Recommendation:** a new `libs/starlang` holds the language-neutral lexicon and selector; `stdlib/stdlib/lang/en/` holds a rule table ported from `inflect` plus the standard library's enumerated action verbs. No English knowledge in `starcore`.

---

## 5. World model and the large-world story

This is the section that has to earn the project's existence.

### 5.1 Where existing systems lose

The performance problems in large IF games have three specific causes, and each needs a distinct answer:

| Cause | Symptom | Our answer |
|---|---|---|
| **Scope computation** is O(objects in scope) per noun, per turn, and scope is recomputed from scratch | Turn time grows with room contents and with carried inventory | Incremental, cached scope with dirty-tracking (§5.4) |
| **Rulebook dispatch** is a linear scan over every rule for the action, each testing its own applicability | Turn time grows with total rules in the game — i.e. with game size, globally | Rules indexed by (action, class/trait) at compile time (§7.3) |
| **Every-turn / daemon processing** runs for all registered objects regardless of location | Turn time grows with total live NPCs and devices in the game | Sector-scoped simulation (§5.3) with explicit offstage modelling |

Note that the second and third are *global* — they get worse just from the game being big, even if the player is standing in an empty room. That is the thing to kill.

### 5.2 Object storage

Objects are stored in a **structure-of-arrays** store, not as heap-allocated polymorphic nodes.

```cpp
using ObjectId = Handle<struct ObjectTag>;   // 32-bit: 24-bit index + 8-bit generation

class WorldStore {
    // Parallel arrays, indexed by ObjectId::index()
    std::vector<ClassId>    class_;
    std::vector<SectorId>   sector_;
    std::vector<ObjectId>   parent_, firstChild_, nextSibling_, prevSibling_;
    std::vector<Relation>   parentRelation_;    // in / on / under / carried / worn / part_of
    std::vector<uint64_t>   traitBits_;         // up to 64 traits hot-path testable
    PropertyStore           props_;             // see below
    // ...
};
```

- **Containment is an intrusive tree**, not a per-object vector. Moving an object is four pointer writes and touches no allocator. Iterating a container's contents is a sibling walk with good locality.
- **The parent link carries a relation** (`in`, `on`, `under`, `behind`, `carried`, `worn`, `part_of`), which generalises your `holding_type` cleanly and means `container` vs `supporter` is data, not a class distinction the engine cares about.
- **Traits are a bitmask** for the first 64 traits (the stdlib will use ~20), so `has_trait(obj, openable)` is a single AND. Beyond 64, a spillover set.
- **Properties**: the common case is that most objects of a class use the class default. `PropertyStore` therefore stores only *overrides*, in a per-property open-addressed map from `ObjectId` to value, with the class default as fallback. A game with 30,000 rooms where 200 have a non-default `condition` stores 200 entries, not 30,000. This is also exactly the representation the save-delta system wants (§14.3).

Rough budget: a 50,000-object world with ~4 overridden properties each fits in well under 20 MB. That is small enough to keep entirely resident on desktop; sector streaming (next) matters for mobile/web and for *simulation* cost more than for memory.

### 5.3 Sectors

**[DECISION]** The notes' "maps" are named **sectors**. A sector is:

- the unit of **authoring organisation** (a directory of `.star` files);
- the unit of **loading and unloading**;
- the unit of **simulation scope** — daemons, every-turn rules and NPC AI run only for the active sector set.

```stardata
sector = {
    id = station_alpha
    display_name = $sector_station_alpha
    # Sectors adjacent enough that we keep them warm
    neighbours = { docking_ring maintenance_deck }
    # What happens to this sector when the player leaves
    on_deactivate = serialize          # serialize | freeze | discard_changes | never
    # Optional coarse simulation while the player is elsewhere
    offstage = {
        model = catch_up               # none | catch_up | simulate | continuous
        script = station_alpha_offstage
    }
}
```

**Active set.** At any moment the runtime holds: the *current* sector (fully live), its declared `neighbours` (loaded, simulated at reduced rate — see below), any **pinned** sectors (below), and everything else unloaded. Transitions are triggered by taking an exit whose target lives in another sector.

**Pinned sectors.** `on_deactivate = never` marks a sector that stays fully live regardless of where the player is. Your starship case is exactly right and, I think, more load-bearing than it first appears: the alternative — placing a stand-in copy of the ship's NPC in whatever sector the player is standing in — is the sort of workaround that quietly corrupts a world model, because now there are two Kira and the author has to remember which one is real.

Static `never` is not quite enough, though, because ship membership changes over the course of a game. So pinning is also a runtime operation:

```stardata
effects = {
    pin_sector   = { sector = ship_wayfarer  reason = player_owns_ship }
    unpin_sector = { sector = ship_wayfarer  reason = player_owns_ship }
}
```

Pins are **reference-counted by `reason`**, so two independent systems pinning the same sector don't un-pin each other, and a save records the outstanding pins. A sector declared `on_deactivate = never` simply starts with a permanent pin.

The cost is real and should be visible: a pinned sector is fully simulated every turn forever. Starforge's build summary reports the statically-pinned set, and the debugger shows the live pin set with reasons, so "why has my turn time crept up" has an obvious first place to look.

**Communication across sectors.** Pinning solves presence, but the radio case also needs the conversation itself to work at a distance. Dialogue (§11) therefore does not require the participants to be co-located — a dialogue's `participants` may be in any *loaded* sector, and `reachable`/`visible` conditions are simply false for remote participants, which is the correct answer for "can I hand Kira the wrench over the radio". Actions that need presence fail naturally; talking does not.

**Cross-sector references.** A `ref<room>` may point into an unloaded sector. Such references resolve to a **stub**: id, class, name, and any properties the author marks `always_resident = yes`. This is what makes "the guard is currently in the mess hall three sectors away" answerable without loading the mess hall. Any operation needing more than the stub triggers a load — and in development builds, logs a warning, because that is usually an authoring mistake.

**Offstage simulation.** Four models, set game-wide as a default and overridable per sector. The trade-off between them is discussed properly in §10.1.3, since it is really a question about NPC schedules.

- `none` — the sector is frozen; no time passes for it.
- `catch_up` — on reactivation, schedules and timers are solved forward analytically from the current clock rather than stepped. Cheap and O(1) in elapsed time. Adequate for "the shopkeeper went home at 18:00". The default.
- `simulate` — on reactivation, every intervening turn is actually run for that sector's actors. Expensive, correct, and the only model under which offstage NPCs can affect each other.
- `continuous` — a per-sector Lua function runs every N turns regardless of where the player is, for sectors that must genuinely progress on their own schedule (a countdown, a war). Explicit opt-in, because this is exactly the global cost §5.1 exists to eliminate.

**The stress test.** `tests/corpus/stress/` should contain a generated game with 20,000 rooms, 60,000 objects, 500 NPCs across 40 sectors, and CI should assert p99 turn time. **Target: under 5 ms per turn on a 2020-era laptop, and flat as world size grows.** Anything else and the differentiator has not been delivered.

### 5.4 Scope

Scope — the set of objects the player can currently refer to — is computed once per turn and cached, then invalidated surgically.

```
scope(actor) =
    contents*(location(actor))            # transitively, through open/transparent containers
  ∪ contents*(actor)                      # inventory, worn items
  ∪ parts*(everything above)              # part_of components
  ∪ author-declared additions             # `in_scope_when` rules
  − author-declared removals              # darkness, blindness, containers closed
```

Implementation notes:

- The cached scope is a **sorted vector of ObjectId plus a bloom filter over name hashes**. Parser noun resolution first tests the bloom filter (rejecting almost all non-matches in one memory access), then binary-searches.
- Invalidation is driven by the world store: any `move`, any change to a property flagged `affects_scope` (open, transparent, lit), and any actor location change sets a dirty bit. Nothing else recomputes.
- **Scope is per-actor and cached only for actors that need it** — the player, plus any NPC currently executing a scope-dependent behaviour.
- Darkness is a scope filter, not a special case scattered through the parser.

### 5.5 Determinism

Save/restore, replay-based testing, and any future multiplayer or "share your transcript" feature all require that the same inputs produce the same outputs.

- **All randomness goes through a seeded engine** owned by the world state and serialised with it. `math.random` is replaced in the Lua sandbox with the world RNG. There is no other source of randomness available to authors.
- **`decimal` is fixed-point**, not IEEE float, precisely so that RPG damage formulas produce identical results on every platform including WASM. Floats are available as `float` for anything cosmetic, and are excluded from save state.
- **Iteration order is defined.** Any engine API returning a collection returns it in a documented deterministic order (by ObjectId, or by declaration order). Lua's `pairs()` is replaced with an ordered variant.

### 5.6 Time, duration and the calendar

Your answer to Q9 — seconds-accurate plumbing in the core, a one-minute-per-action default in the stdlib, and calendar dates — is the right shape, and it makes the time model a core subsystem rather than a convenience. Getting it in early is important: schedules (§10.1), status-effect expiry (§9.1), combat rounds (§9.3) and offstage catch-up (§5.3) all read from it, and retrofitting a finer clock under them later would be miserable.

**Two clocks, deliberately separated.**

- The **turn counter** is a monotonic integer counting rounds (§7.1). It is what save deltas, undo depth and daemon scheduling are keyed on. It has no relationship to fiction.
- The **world clock** is a 64-bit signed count of **ticks** since the calendar epoch. One tick is one second by default (`tick_seconds` is configurable if a game wants finer granularity, and negative values are legal so prehistory works). It is what schedules, dates and durations read.

**Actions have durations.**

```stardata
action = {
    id = take
    duration = default          # inherits the game default
    advances_turn = on_success  # on_success | always | never
}

action = {
    id = inventory
    duration = 0
    advances_turn = never       # an "out of world" action
}

action = {
    id = pick_lock
    duration = 300              # five minutes
}
```

The stdlib sets the game default to 60 ticks — one minute per action, the familiar convention. A combat ruleset sets its combat actions to 6 ticks, which is d20's round, and does so without the engine knowing anything about combat.

**How the clocks interact.** Each actor carries a `busy_until` tick. The round's duration is the duration of the *player's* action; the clock advances by that much in the ADVANCE phase (§7.1). An actor whose chosen action costs more than the round advanced is still busy when the next round starts and is skipped in the actor loop until the clock reaches its `busy_until`.

This gives the behaviour you described directly: an NPC whose schedule calls for a one-minute task, in a game where the player's current action costs six seconds, simply stays busy for ten rounds. It also gives, for free, the "you spend five minutes picking the lock and when you look up the patrol has moved on" effect, which is otherwise fiddly to author.

Two consequences worth stating because they will surprise someone:

- **A long player action gives every NPC many turns at once.** Waiting an hour should not let a guard take 3,600 actions. The engine therefore *batches* NPC time during a long player action: actors resolve their schedules analytically over the elapsed span (the same machinery as `catch_up`, §5.3) rather than stepping. A ruleset that wants full stepping opts in per action with `long_action_model = simulate`.
- **`advances_turn = never` actions run no actor loop at all.** Checking your inventory does not give the enemy a free swing. This matches player expectation everywhere and is what the flag is really for.

**Calendars are author-defined**, because your games are not necessarily set on Earth:

```stardata
calendar = {
    id = terran_standard
    tick_seconds = 1
    seconds_per_minute = 60
    minutes_per_hour = 60
    hours_per_day = 24
    days_per_week = 7
    day_names = { monday tuesday wednesday thursday friday saturday sunday }
    month = { id = january  days = 31 }
    month = { id = february days = 28  leap_days = 1 }
    # ... leap rule as a script if the calendar is exotic
    leap_rule = gregorian_leap
    epoch = "2384-03-11 06:00:00"
    display_default = $fmt_datetime
}
```

Schedules, quest deadlines and time-of-day conditions are written against the calendar's units (`from = "07:00"`), and the compiler converts them to ticks. A game that wants a 26-hour Martian day changes one declaration and everything downstream follows.

**One universal clock. [RESOLVED]** Per your call: a single global tick count — the stardate — with relativistic effects handwaved in the Star Trek manner. One second is one second everywhere, and no location experiences a different elapsed duration from any other. This is a real simplification and not just a cop-out: independent clocks would mean every cross-sector reference (§5.3) carries a timestamp that has to be translated, every save records N clocks, and every schedule condition needs to know which frame it is evaluated in. That is a large amount of machinery in service of a plot device.

What *does* survive, because it is not relativity but merely rotation, is **local time**:

```stardata
sector = {
    id = kepler_iv_surface
    local_clock = { calendar = kepler_iv  offset = "+04:30" }
}
```

A sector may name a calendar (§5.6's `calendar` block — a planet with a 30-hour day is just a different calendar) and an offset. This affects only two things: how the clock is *displayed* to the player, and how schedule times written in that sector's terms are compiled to absolute ticks. An NPC on the planet keeps a 30-hour-day routine; the engine stores the resulting wake and sleep times as ordinary tick values on the one global timeline. Nothing downstream needs to know.

The stardate itself is a display format over the tick count, so `display_default` can render "Stardate 61947.3" while the engine reasons in seconds.

---

### 5.7 Objects in more than one place

Doors referred to from either side, the sky, the ground, a distant mountain, the ever-present hum of the reactor. You're right that this needs core support, and right to be unenthusiastic about the received solution.

#### 5.7.1 Why the teleport hack hurts

The standard trick — quietly move the object to the player's room on arrival — fails for a reason worth naming precisely, because the name suggests the fix. **It converts a presentation concern into a world-state mutation.** Every quiet move is a real edit to the containment tree, and so it:

- fires whatever rules watch for movement, unless each one is special-cased not to see it;
- dirties the scope cache (§5.4) on every room change, for every backdrop;
- writes a save delta (§14.3) every time the player walks through a door, so a mechanism that stores *nothing* in principle churns the save file in practice;
- shows up in undo (§16.5), so undoing a move has to unwind bookkeeping the player never caused;
- is visible to any rule that iterates a room's contents, which is how "the sky is in my inventory" bugs happen;
- emits a percept (§10.2) unless suppressed, so NPCs can in principle notice the sky arriving.

The bugs you describe — floating objects not updated when the player moves — are the *symptom*. The cause is that the mechanism tells the world model something false and then has to maintain the lie at every site that might notice. Adding another special case at each site is why it stays fragile.

So the answer is not a better teleport. It is to stop moving things.

#### 5.7.2 Presence is a separate relation from containment

Our architecture makes this cheap in a way Inform's does not, and it is worth being explicit about why. In Inform, being in scope is *derived from* containment, so the only way to be in scope somewhere is to be there. In STAR, scope is already a computed set with declared additions and removals (§5.4). Adding presence is one more union term, not a new mechanism.

Concretely, the world store (§5.2) gains a second relation alongside the containment tree:

| Relation | Cardinality | Stored as |
|---|---|---|
| **containment** (`in`, `on`, `carried`, `worn`, `part_of`, …) | one parent | intrusive tree, as today |
| **presence** (`present_in`) | many rooms | a sparse room-set per object, plus a reverse index room → objects |

The reverse index is what scope reads, so the cost at scope time is one lookup and a set union — and it is *zero* for the overwhelming majority of games, which have a handful of backdrops.

```stardata
backdrop = {
    id = the_sky
    traits = { scenery fixed_in_place }
    present_in = { antecourt kepler_iv_surface observation_deck }
}

# Enumeration gets tedious, so a query form. Resolved at compile time into a
# concrete room set, which keeps scope-time cost at zero.
backdrop = {
    id = reactor_hum
    traits = { scenery fixed_in_place }
    present_in = { where = { in_sector = station_alpha  condition == breathable } }
}
```

A predicate that can change during play (`where = { lit == yes }`) requires `dynamic = yes` and is then evaluated at scope time rather than baked. Making the author opt in keeps the expensive form visible rather than accidental.

What this buys, point for point against the list in §5.7.1: no movement rules fire, the scope cache invalidates only when presence actually changes, the save stores one record for the object's whole life, undo has nothing extra to unwind, room-contents iteration never sees it unless asked, and no percept is emitted. Every failure mode of the teleport hack is absent by construction rather than by special case.

#### 5.7.3 Two-sided objects

Doors are not backdrops, and conflating them is the other half of the mess. A door is present in exactly two rooms, and it *presents differently from each side*: a different name, possibly a different description, and necessarily a different direction of travel.

The two established answers are both half-right. Inform uses one object with one name, which gets the shared state right and the asymmetry wrong. TADS uses two linked objects, which gets the asymmetry right and then has to mirror `open` and `locked` between them, which is the kind of thing that desynchronises.

Proposal: **one object, shared state stored once, per-side presentation as facets.**

```stardata
door = {
    id     = airlock_hatch
    traits = { openable lockable fixed_in_place }
    open   = no                       # shared state — one property, one record
    locked = yes

    side = { room = airlock
             direction   = out
             name        = $hatch_inner
             description = $hatch_inner_desc }

    side = { room = docking_gantry
             direction   = in
             name        = $hatch_outer }
}

room = { id = airlock  exits = { out = airlock_hatch } }
```

- `side` is sugar over `present_in`: it declares presence in two rooms *and* the facet to use when observed from each.
- An `exits` entry may name a door instead of a room; the engine resolves through it to the far side. This is Inform's model and it is right — it keeps the map graph and the door object from drifting apart.
- Facet resolution happens at name-and-describe time, keyed on the observer's location. It costs a lookup and only for objects that have facets.
- A side may omit anything; omitted fields fall back to the object's own properties.

#### 5.7.4 Consequences the core must handle

**Sector residency (§5.3).** A door spanning two sectors is exactly the transition trigger, so this is load-bearing rather than incidental. Rule: a multi-located object is resident whenever **any** sector it is present in is loaded, and it belongs to no single sector's object table. Starforge places such objects in a shared table loaded alongside any of their sectors. Getting this wrong would make every cross-sector door a null dereference.

**Portability is forbidden.** An object with `present_in` or `side` MUST NOT be portable — if you could take it, there is no sensible answer to where it went. This is a compile error, not a runtime one.

**Percepts (§10.2) may travel through.** An open door present in two rooms is a natural conduit for sound, which is otherwise fiddly to author. Worth having, but as an explicit `conducts = { sound }` on the object rather than as implicit behaviour, because "the guard heard you through a closed door" is a bug and "through an open one" is a feature.

**What this is not for.** An object needing *different state* per location is not one object. A blood trail that can be examined once per room is several objects sharing a template, and the format should not blur that. `present_in` means one thing that is genuinely in several places; anything with per-location state is several things.

---

## 6. The parser

### 6.1 Pipeline

```
raw input
  → normalise (case, punctuation, "then"/"." splitting into multiple commands)
  → tokenise (words → dictionary word ids; unknown words flagged, not fatal)
  → grammar match (against the action grammar trie)
  → noun-phrase resolution (against cached scope)
  → disambiguation (may suspend and ask)
  → Action instance { action, actor, noun, second, direction, text, number }
```

### 6.2 Grammar lines

Your `match = { "get/take/grab [something]" }` syntax is good and should stay. Full token vocabulary:

| Token | Matches |
|---|---|
| `[something]` | One object in scope |
| `[things]` | One or more objects, supporting `all`, `all except X` |
| `[something held]` | Object carried by the actor |
| `[something preferably held]` | Prefers held objects in disambiguation, does not require |
| `[something preferably weapon]` | Prefers objects of a class or trait in disambiguation, does not require. Generalises the `held` form |
| `[someone]` | Object with trait `animate` |
| `[direction]` | A compass/vertical direction |
| `[text]` | Free text, unresolved (for `ask X about Y`, `consult`) |
| `[number]` | An integer |
| `[topic]` | A declared conversation topic (§11) |
| `[class:weapon]` | **Only** objects of a class or trait; anything else fails to parse. Use sparingly — see below |
| `word/word/word` | Alternative literal words |
| `word?` | Optional literal word |

**Prefer broad tokens.** A narrow `[class:…]` token means a non-matching noun fails *in the parser*, so `DRINK LAPTOP` yields "you can't see any such thing" about a laptop sitting in plain view. The refusal belongs in the world, not the grammar: match `[something]`, then let a restriction produce a real message. That restriction also narrows the noun's static type for the rest of the action (spec §8.8.3), so nothing is lost by being permissive — and where behaviour genuinely differs by class, that is what rules are for.

Reserve `[class:…]` for cases where a non-match should truly be unparseable rather than refusable: `[direction]`, `[topic]`, and lines distinguished by their own literal words. Where a class matters only for choosing between candidates, `[something preferably weapon]` says so without making anything unparseable.

Grammar lines compile into a **trie over word ids** with actions at the leaves, so matching is O(words typed), independent of the number of grammar lines in the game. Multiple matches produce ranked candidates; ranking prefers (1) more literal words matched, (2) more specific noun tokens, (3) later-declared lines (so a game can override the stdlib).

### 6.3 The command list

`list-of-inform-commands.txt` is a good target for the stdlib's coverage. My recommendation on scope, per the notes' "we probably won't need to support quite all of these":

**Ship in `stdlib` (the ~45 verbs that carry real weight):** take, drop, put in/on, examine, look, look under/behind, search, open, close, lock, unlock, go, enter, exit, climb, push, pull, turn, switch on/off, wear, take off, eat, drink, give, show, throw at, attack, touch, taste, smell, listen, read, consult about, wait, again, inventory, and the meta commands (save, restore, restart, quit, undo, score, verbose/brief, transcript, pronouns).

**Ship in `stdlib` as *stubbed* actions** — recognised, with a graceful default refusal, so the player never gets "that's not a verb I recognise" for a reasonable attempt: burn, buy, cut, jump, kiss, pray, rub, sing, sleep, squeeze, swim, swing, tie, wake, wave, think, sorry, yes, no. Authors override the ones their game cares about.

**Do not ship:** the Inform legacy oddities (`nouns` as a synonym for `pronouns`, `verify`) and anything Z-machine specific.

**Add, because the intended games need them:**

- `talk to [someone]` — opens dialogue (§11); `topics` re-prints the current choices for single-window frontends.
- `[someone], [command]` and `party, [command]` — ordering party members and companions (§9.5).
- `status` / `journal` / `map` — the command-line equivalents of the dockable windows, for frontends that have none (§12.3).
- When a ruleset is loaded: attack with, cast, use, equip, unequip, and the quest commands. These come from the ruleset's own grammar, not `stdlib`.

### 6.4 Disambiguation

Disambiguation must be able to **suspend the turn** and resume. Implementation: the action pipeline is a resumable state machine, not a recursive call chain, so "Which do you mean, the brass lantern or the steel lantern?" parks a continuation rather than re-entering the parser. This also gives us, for free, the machinery needed for dialogue choice prompts (§11) and for "are you sure?" confirmations.

Ranking heuristics, in order: recently-referred-to (pronoun memory), held by actor, in the actor's location vs. in a container, matching more of the typed adjectives, author-set `disambiguation_priority`.

#### 6.4.1 The unselectable-object problem

You flagged the sharp edge, and it deserves treating as a design requirement rather than a known wart. Given a `box` and a `red box` in the same room:

```
> take box
Which do you mean, the box or the red box?
> box
Which do you mean, the box or the red box?
```

Nothing the player types resolves to the plain box, because every name it answers to is also a name the red box answers to. The player is in a loop with no exit but a synonym they have not been told exists. This is a **strict-subset** problem: whenever one candidate's name set is a subset of another's, the smaller object is unreachable by name.

TADS 3 takes the pragmatic way out and accepts "the first". That works, and we should accept it too, but it treats the symptom.

**The fix worth having is at compile time.** The condition is statically detectable: for any two objects that can be in scope together, compare their name-and-adjective sets, and if one is a subset of the other, warn. Starforge's static analysis (§13.3) should report:

```
warning: `box` cannot be selected by name when `red_box` is in scope
  note: every name of `box` is also a name of `red_box`
  note: both are reachable in `storage`
  help: give `box` a distinguishing adjective, or set
        disambiguation_name = "plain box"
```

That converts an unfixable play-time dead end into a build-time message with a fix-it, which is the same move as the inferred-plurals report in §4.10.1: an unbounded runtime problem turned into a finite list the author reviews once.

Scope analysis here is necessarily approximate — "can be in scope together" is undecidable in general — so the analysis should use a cheap over-approximation (same room, or same container, at any point in the compiled initial state) and be suppressible per object. False positives that cost a suppression are much better than the false negative, which costs a stuck player.

#### 6.4.2 Runtime fallbacks

Three, in order of preference, and the machinery for all of them already exists:

1. **A distinguishing name.** `disambiguation_name = "plain box"` supplies wording for the question and is accepted as input. This is the authored answer and the one the compile-time warning steers toward.
2. **Numbered choices.** The frontend protocol already carries `InputRequest{kind: choice}` (§12.2) and the turn state machine already suspends (above), so disambiguation can present a numbered list and accept a number — reusing exactly the mechanism dialogue uses. This is also the answer for mobile (§12.5), where typing a distinguishing phrase is the worst available interaction.
3. **Ordinals.** "the first", "the second", matching the presented order. Cheap, familiar from TADS, and a reasonable thing to accept even when numbered choices are displayed.

A frontend declaring `input: [line]` only — Glk, CLI — gets the numbered list printed inline and accepts the number as typed text, so no capability is required beyond what already exists.

#### 6.4.3 Indistinguishable candidates

The opposite case, and it must not be handled by asking. Ten identical clones, or two identical coins, produce "Which do you mean, the coin or the coin?" — which is worse than any answer.

**Rule:** when candidates are indistinguishable by name and by every property the player can observe, the parser MUST choose one **deterministically and silently** rather than prompting. Determinism matters for the usual reasons (§5.5): the same command in the same state must pick the same coin, or saves and replay diverge.

This is required whether or not compile-time replication (spec §8.9) is ever implemented, since two hand-declared identical objects raise it equally, so it belongs in the Phase 1 parser.

---

## 7. Actions and rules

### 7.1 The turn

Your Kerkerkruip point is well taken and it was a real flaw in v0.1. Shoving NPC actions into an `every_turn` bucket is precisely the workaround Inform authors are forced into, and it fails for the reason you'd expect: an "every turn" rule cannot be *interleaved* with the player's action, cannot take a reaction to it, and cannot be ordered by initiative against it. Kerkerkruip has to replace Inform's turn sequence rules wholesale to get out of that hole.

So the turn is rebuilt as an **initiative-ordered loop over actors**, with the ruleset's hook points named and first-class. A "turn" is a **round**: every eligible actor has had its action.

```
ROUND N
────────────────────────────────────────────────────────────────────────
 1  ROUND_START      global ruleset hooks that must precede everything:
                     weather, ambient events, world-level timers
 2  UPKEEP           per-actor, in initiative order: status effects tick,
                     durations decrement and expire, regeneration,
                     ongoing damage, resource recovery
 3  INITIATIVE       build this round's actor order.
                     Out of combat: player first, then eligible NPCs in
                       stable declaration order. No rolls, no cost.
                     In combat: the loaded ruleset supplies the order.
 4  ACTOR LOOP       for each actor in order, skipping any actor whose
                     `busy_until` is still in the future (§5.6):
     4a  DECIDE      player  → parse input (may suspend, §6.4)
                     NPC     → behaviour layers (§10.1)
                     party   → prompts the player if the member's
                               `control` says so (§9.5), else as an NPC
     4b  BEFORE      @before rules; may veto silently or loudly
     4c  CONDITIONS  silent gates; a failing rule is skipped
     4d  RESTRICTIONS loud gates; failing prints failureMsg, the action
                     fails, jump to 4g
     4e  EFFECTS     world mutation; may redirect via try_action (§7.2)
     4f  REPORT      successMsg and rule messages, in phase order
     4g  REACTION    the target, and any observer the ruleset qualifies,
                     may take a reaction: parry, dodge, opportunity
                     attack, "the guard shoves you aside". Nested, and
                     itself runs 4b–4f. Empty out of combat.
     4h  AFTER       percepts dispatched (§10.2). Observers update
                     knowledge and may push goals — they do not act
                     here; they act in their own slot.
 5  ROUND_END        top-level daemons; timed events whose tick has come;
                     end-of-round ruleset hooks (bleeding out, effects
                     that expire "at the end of the round")
 6  ADVANCE          world clock advances by the round's duration (§5.6);
                     schedules re-evaluated; turn counter increments;
                     undo snapshot and autosave if configured
```

Every one of phases 1, 2, 3, 4g and 5 is a **registration point** a library hooks into by declaration (§9.3), not a place it has to monkey-patch.

**Mapping onto your d20 example.** Your understanding is right, and here is where each of your six steps lands:

| Your step | Phase |
|---|---|
| 1. PC attacks, target can react | 4a–4g, player's slot |
| 2. Friendly NPC attacks, target can react | 4a–4g, companion's slot |
| 3. Enemy NPC attacks, target can react | 4a–4g, enemy's slot |
| 4. Non-combat NPCs react | 4h *notices*; the acting happens in their own slot — see below |
| 5. Daemons and timed events | 5 |
| 6. Non-combat NPCs follow schedules | their 4a; phase 6 advances the clock their schedules read |

**The one place I'd restructure your model**, and I want to be explicit that it is a genuine trade-off rather than a correction: I'd put non-combat NPCs **inside the same initiative loop** rather than giving them separate later phases. Reasons:

- One code path for "an actor takes an action". Two paths means two sets of bugs, and it means a non-combat NPC's action can't be reacted to, can't be interrupted, and can't be the target of a rule that assumes the normal pipeline.
- Their actions become observable by the same percept machinery. A janitor walking into a firefight is something the combatants should notice, which requires him to have acted *within* the round, not after it.
- It dissolves the awkward question "what happens if a scheduled NPC walks into an ongoing fight" — he takes his slot, and initiative decides when.

The cost is narration: a scheduled NPC's mundane business can now interleave with combat beats, producing "Vex ambles in and starts mopping" between two sword swings.

The first mitigation is straightforward — **initiative places non-combat actors last by default**, so a round reads as the fight first, then the world around it.

v0.1 of this section proposed a second one — suppressing non-combat NPCs' *reporting* while leaving their *acting* intact — and you're right to be split on it. I now think it should be dropped outright, for a reason worth stating because it generalises: **an actor that acts invisibly is worse than one that acts incongruously.** The world changes, the player is told nothing, and three rooms later they discover the janitor tidied away the thing they needed. That is an unfalsifiable bug from the player's side and a nightmare from the author's. Incongruous narration is at least *visible*, and visible problems get fixed.

Your diagnosis is the right one: this is a **behaviour** gap, not a narration gap. The janitor mopping through a firefight is only absurd because nobody told him there was a firefight. So the fix belongs in §10.1's interrupt layer, and it needs a default so that it cannot be an oversight:

```stardata
# supplied by the ruleset, on the base animate class
class_extension = {
    of_class = person
    prop_def = {
        combat_response = enum<combat_response_enum>
    }
    combat_response = flee        # the ruleset's default for ordinary people
}

enum = {
    id = combat_response_enum
    values = { join_ally join_hostile defend_self flee panic observe ignore }
}
```

with the ruleset supplying a standing interrupt keyed on the combat percept (§10.2) that dispatches on this property, and a **game-level default** in the manifest for authors who want a different baseline:

```stardata
simulation = {
    default_combat_response = flee
}
```

Three things this gets right that the suppression approach did not:

- **The oversight case now fails safe.** An NPC the author never thought about does something sane — civilians scatter, guards engage — rather than obliviously mopping. Nobody has to remember to code a reaction for every NPC, which was your objection.
- **The janitor bit becomes available, and becomes a choice.** `combat_response = ignore`, plus a bark table of world-weary mop commentary, gets you exactly the "there go Bob and Gary spilling blood again" effect you described — and gets it as something the author *decided*, in one line, rather than as a default nobody noticed. That is the version worth having: the joke lands because it is deliberate.
- **It is ruleset policy, not engine behaviour.** The core knows nothing about combat; it supplies the percept and the interrupt layer, and the ruleset decides what "combat is happening near me" means. A ruleset with no combat at all defines none of this and pays nothing.

`observe` deserves a mention as a distinct value from `ignore`: the NPC stops what they were doing and watches, which costs their slot, produces no absurdity, and leaves them a witness afterwards (§10.3). For a lot of background characters that is the most useful default of the seven, and it is a reasonable alternative to `flee` for the ruleset baseline. Worth deciding from play rather than from theory.

**Eligibility, and the cost control.** Only actors in the **attention set** enter the loop: actors in the current sector, in pinned sectors (§5.3), and in loaded neighbour sectors within a configurable room-graph radius. Everyone else is handled by the offstage model (§5.3, §10.1.3). This is what keeps a game with 500 NPCs at the same per-round cost as a game with 5.

### 7.1.1 The initiative order is mutable

You pointed out that we already have to handle actors leaving the order (death, flight, unconsciousness) and joining it (someone walks in after the fight has started), and that observation settles the question — those two cases are not optional, they are the minimum viable combat, and any implementation that supports them has already paid for the hard part.

The hard part is not the list operations. It is **iterating a list while it is being mutated**, which is where this kind of code reliably goes wrong: an actor is removed, the index shifts, and the next actor is silently skipped or served twice. So the loop is specified to make that class of bug unrepresentable:

- The round holds an ordered list of actor ids plus an `acted` set.
- Iteration advances a **cursor over actor identity**, not an index. At each step it takes the first actor in order that is not in `acted`, and adds it on completion.
- Removing an actor mid-round removes it from the list; if it has already acted, nothing happens; if not, it simply never comes up.
- Inserting an actor mid-round places it at its rolled position; if that position is before the cursor, it does not act this round (it arrived too late), which is both correct and the behaviour every tabletop table settles on anyway.

Given that, the operations you were ambivalent about cost almost nothing on top:

| Operation | Needed for | Implementation |
|---|---|---|
| `remove` | Death, flight, incapacitation | **Mandatory.** Drop from list. |
| `insert` | Arriving mid-combat | **Mandatory.** Insert at rolled position. |
| `defer` | "I'll go after Kira" | Move the actor later in the current order; `acted` untouched. |
| `ready` | "I attack the first person through the door" | The actor spends its slot registering a trigger. When the trigger fires, the action runs as a **reaction** (§7.1, phase 4g) — machinery that already has to exist. |

`defer` and `ready` are therefore a few dozen lines on top of the mandatory two, which is a different proposition from the "small but real" cost I implied. **Recommendation: build all four in the core, and let Starscape decide whether to expose `defer` and `ready` as player-facing commands.** If they turn out not to earn their place at the table, the ruleset simply doesn't grant them and the core cost is a cursor that was already needed.

On the debugger concern I raised: it goes away with the right presentation. The round view renders as a timeline with the mutations marked in place — "Vex removed (killed)", "Kira deferred to after Bolt", "Guard 3 joined at position 2" — which is more legible than a static order that quietly stopped matching reality.

**Q3, resolved.** `advances_turn` is a property of the action — `on_success` (the default), `always`, or `never` — with a game-level default settable in the project manifest. And per your note: an action whose effects redirect to another action consumes **one round in total**, not two. §7.2 specifies how.

### 7.2 Conditions and effects

Your `restrictions` syntax generalises into a **condition language** used identically in restrictions, rule `conditions`, `when` clauses, dialogue node availability, quest predicates and NPC behaviour guards. Learn it once, use it everywhere.

```stardata
restrictions = {
    # implicit AND
    carrying = { holder = actor  obj = noun }
    NOT = { noun = { of_class = fixed_in_place } }
    OR = {
        actor = { strength >= 14 }
        carrying = { holder = actor  obj = crowbar }
    }
    # scripting escape hatch
    script = { fn = can_lift_the_slab  failureMsg = $slab_too_heavy }
}
```

Built-in condition predicates: `of_class`, `has_trait`, `carrying`, `wearing`, `containing`, `in_location`, `in_sector`, `visible`, `reachable`, `is`, property comparisons, `flag_set`, `quest_state`, `random_chance`, and the combinators `NOT` / `OR` / `AND` / `COUNT_AT_LEAST`.

Effects, similarly:

```stardata
effects = {
    move = { obj = noun  to = actor  relation = carried }
    set = { target = noun  prop = open  value = yes }
    add = { target = actor  prop = credits  amount = 50 }
    remove_from_play = { obj = noun }
    trigger = { event = alarm_raised  in_sector = station_alpha }
    start_quest = { quest = find_the_captain }
    pin_sector = { sector = ship_wayfarer  reason = player_owns_ship }
    try_action = { ... }                       # §7.2.1
    enter_dialogue = { ... }                   # §7.2.2
    script = { fn = complicated_thing }
}
```

Effects execute in declaration order — which, since v0.2 dropped the ordered/unordered distinction (§4.2), is now simply how every block behaves. The v0.1 inconsistency you flagged is gone.

### 7.2.1 Action redirection

A real omission from v0.1, and you're right that it is load-bearing: `TAKE APPLE` when the apple is in a box redirecting to `REMOVE APPLE FROM BOX` is not an edge case, it is most of what a standard library does. Inform's `try ... instead`, TADS's `replaceAction`, ADRIFT's `Execute <Task>`.

```stardata
rule = {
    of_action = take
    when = { noun = { has_trait = portable } }
    conditions = { noun = { held_by = { of_class = container } } }
    effects = @override {
        try_action = {
            action = remove_from
            noun   = noun
            second = HolderOf(noun)
            # what happens to the *outer* action if the inner one fails
            on_failure = abort        # abort | continue | succeed
            # whether the inner action reports its own success message
            report = yes              # yes | no | only_on_failure
        }
    }
}
```

Semantics, spelled out because this is exactly the mechanism whose corner cases bite:

- **The inner action runs the full pipeline** — its own before/conditions/restrictions/effects/report, and its own rules. It is not a shortcut to the inner action's effects block.
- **One round, total.** The round's turn consumption and duration are those of the *outermost* action, per your note. An inner action's `duration` and `advances_turn` are ignored. If an author genuinely wants the inner action's cost instead, `inherit_duration = yes` says so.
- **`on_failure`** decides the outer action's fate: `abort` (the default — the outer action fails, and the inner action's failure message is what the player sees), `continue` (the outer action proceeds to its remaining effects), or `succeed` (the failure is swallowed; rare, but needed for "try to be polite about it and move on").
- **Recursion is bounded.** A depth limit (default 16) turns a redirect cycle into a caught authoring error naming the cycle, not a hang. Starforge additionally detects *statically* provable cycles at build time.
- **The actor is inherited** unless overridden, so a redirect inside an NPC's action stays that NPC's action.
- **`try_action` is available from Lua** as `star.try_action{...}`, returning the inner verdict, which is what makes the scripted-rule form in §8.4 able to do the same job.

A `continue`-flavoured convenience, `also_try`, is not proposed as separate syntax — `try_action` with `on_failure = continue` covers it, and one construct is easier to teach than two.

### 7.2.2 Entering dialogue from anywhere

The hybrid model you describe — examining a control panel drops you into a "conversation" with a narrator — is common in cRPGs for good reason: it is the cheapest way to give a single interaction real branching without inventing a bespoke UI for it. Making it an effect costs nothing and unlocks the pattern:

```stardata
rule = {
    of_action = examine
    when = { noun = { is = damaged_reactor_console } }
    effects = @override {
        enter_dialogue = {
            dialogue = reactor_console_interface
            node     = main_menu            # optional; defaults to the entry rules
            # optional: what to do with the rest of the turn
            on_exit  = resume               # resume | end_round
        }
    }
}
```

Requirements this places on the rest of the design, all of which are already met:

- Dialogue must be enterable **mid-effects**, which needs the resumable turn state machine from §6.4. The effects block suspends; when dialogue ends, execution resumes at the next effect.
- The **speaker may be an object or absent**. A `dialogue` whose `speaker` is a non-animate object works; one with `speaker = none` is a narrator voice. Portrait, name and styling come from the speaker if there is one (§11.3).
- Dialogue entered this way is still a normal dialogue — it can branch on quest state, run skill checks, and apply effects.

`on_exit = resume` is the default and means the interaction happens inside the examining action, so it costs one round. `end_round` is for dialogues long enough that the surrounding action should be considered spent.

### 7.3 Rule indexing — the key optimisation

At compile time, Starforge builds a dispatch table:

```
(ActionId, ClassId or TraitId) → [RuleId, ...]   sorted by phase, then priority, then load order
```

For an incoming action on a given noun, applicable rules are found by looking up the noun's class chain and trait bits — typically 3–5 table lookups yielding a handful of rules — rather than scanning every rule declared for that action. A game with 5,000 rules dispatches as fast as a game with 50.

Rules whose applicability cannot be determined statically (those keyed only on runtime conditions, e.g. `when = { time_of_day > 20 }`) go into a small per-action "dynamic" list that *is* scanned. Starforge reports the size of that list in its build summary, so an author who is accidentally building an O(n) game finds out.

### 7.4 Rule ordering and conflicts

Precedence, highest first:
1. Explicit `@priority(n)`.
2. Specificity — a rule on `hot_potato` beats one on `thing`. Depth in the class chain, then trait-count.
3. Load order — game overrides library; later-loaded mod overrides earlier.

Starforge emits a **conflict report**: any two rules on the same (action, class) at the same phase and priority, where both have `@override` effects. Silent rule-shadowing is the single most common source of "why doesn't my game do what I said" in Inform, and a build-time report kills most of it.

Starbase should offer a **rule inspector**: pick an action and a class, see the fully ordered, merged pipeline that will actually run.

---

## 8. Scripting: Starscript

### 8.1 Why Lua 5.4, specifically

Lua 5.4 over LuaJIT: LuaJIT is stuck at 5.1 semantics, and its FFI is a sandbox escape by design. It can be built with FFI disabled, but we would be fighting the runtime we chose for speed. 5.4 has an incremental *and* generational GC, integer/float distinction (useful for our determinism story), and to-be-closed variables. The performance gap does not matter for a turn-based text engine where Lua handles the uncommon path.

sol3 for bindings: header-only, zero-overhead in the common case, and its usertype system maps cleanly onto our handle-based object model.

### 8.2 Sandbox

The threat model is: **the player downloads a `.spak` from a stranger and runs it.** That must be as safe as opening a PDF, and ideally safer.

- **No shared global environment.** Each game gets a fresh `_ENV` containing only: the STAR API, and a whitelisted subset of Lua's stdlib — `string` (minus `dump`), `table`, `math` (with `random`/`randomseed` replaced by the world RNG), `select`, `type`, `tostring`, `tonumber`, `ipairs`, ordered `pairs`, `pcall`, `error`, `assert`.
- **Removed entirely:** `io`, `os`, `package`, `require`, `debug`, `load`, `loadstring`, `dofile`, `loadfile`, `collectgarbage`, `rawset`/`rawget` on engine usertypes, `setmetatable` on engine usertypes.
- **Text-only chunk loading.** Chunks are loaded with mode `"t"`. **Precompiled Lua bytecode is never loaded from a game file** — Lua's bytecode verifier is not a security boundary and has a long history of memory-safety escapes. This means Starforge ships Lua *source* in the `.spak` (optionally minified/obfuscated, which is a distribution concern, not a security one). Startup cost of compiling source is a few milliseconds and can be amortised by caching bytecode **in a location the game file cannot write to**, keyed by hash.
- **Instruction budget.** A debug hook fires every N VM instructions; exceeding a per-turn budget raises an error that unwinds cleanly to the turn boundary. Infinite loops in a game become a caught error, not a hang.
- **Memory cap.** A custom `lua_Alloc` enforces a configurable ceiling (default 128 MB), returning failure past it, which Lua handles as an out-of-memory error.
- **No filesystem, no network, no process, no clock.** File access is exclusively through the VFS API, which is scoped to the game's own mount points and cannot escape them. Wall-clock time is not exposed (it would break determinism anyway); a monotonic turn counter is.
- **Coroutines** are permitted and are the mechanism for suspendable behaviours (dialogue, cutscenes, multi-turn NPC actions).

A short **security note document** should be shipped stating exactly these guarantees, so the eventual "is it safe to run STAR games?" question has an answer with substance.

### 8.3 API surface sketch

```lua
-- Objects are lightweight handles; property access goes through metatables
function on_take_slab(ctx)
    local actor, noun = ctx.actor, ctx.noun
    if actor.strength < 14 and not actor:is_carrying(star.obj.crowbar) then
        return star.fail("$slab_too_heavy")
    end
    noun:move_to(actor, star.rel.carried)
    star.emit("$slab_lifted", { obj = noun })
    star.quest.advance("clear_the_rubble", "slab_moved")
    return star.ok()
end

star.on_event("alarm_raised", function(ev)
    for npc in star.sector.current():objects_with_trait("guard") do
        npc.behaviour:push_goal("investigate", { target = ev.location, urgency = 3 })
    end
end)
```

**Correcting the v0.1 design rule.** I wrote "no capability exists only on one side", and you're right that it is false and shouldn't be aspired to. Stardata has no conditionals inside `effects`, no loops, and no arithmetic beyond accumulate-and-compare; Lua has all three. That is not a gap to close, it is the point — the restriction is what makes an effects block renderable as an editor form and analysable by Starforge, and ADRIFT demonstrates that it is livable.

The rule that actually holds, and that I'd hold us to:

> **Stardata is a curated subset of what Lua can express, chosen for editability and static analysis. Everything expressible in Stardata is expressible in Lua. The reverse is deliberately not true.**

> **Corollary:** no *engine capability* is reachable only from Stardata. If a data primitive exists, a script can invoke the same underlying operation. This is what keeps the scripted escape hatch from being a dead end — an author who outgrows the declarative form never has to give up anything to move to a script.

Every mutating call, from either side, is save-safe: there is no engine state that is not serialised.

### 8.4 What is data and what is script

The notes raise this explicitly. Proposed dividing line:

- **Data** expresses *what exists* and *what is true*: objects, classes, properties, grammar, static rules, conditions built from the predicate vocabulary, effects built from the effect vocabulary, dialogue trees, quest structure, NPC schedules.
- **Script** expresses *computation*: anything requiring loops, arithmetic beyond comparison and accumulation, or algorithms.

The test for whether a mechanism belongs in the data vocabulary: *would an author reach for it more than once a game, and can Starbase render an editor for it?* If yes, it is a data primitive. If it is a one-off, it is a script.

Expect the boundary to move as the stdlib is written; that is fine, as long as it moves by *adding data primitives*, not by adding C++ special cases.

### 8.5 Fully scripted rules

Your suggestion, adopted. There is a real cliff between "this rule is declarative" and "this rule needs a script somewhere in it", and forcing an author to decompose a fundamentally procedural rule into four declarative slots each containing a `script =` call is busywork that produces worse code than just writing the function.

```stardata
rule = {
    of_action = take
    # Only the declarative parts that Starforge needs for dispatch (§7.3)
    when   = { noun = { has_trait = cursed } }
    # ...and then one function replaces conditions, restrictions,
    # effects and successMsg entirely.
    script = handle_cursed_take
}
```

The contract:

- `when` stays declarative because §7.3's dispatch index is built from it. A rule with no `when` lands in the scanned dynamic list, and Starforge reports it — the incentive is aligned in the right direction.
- The function receives a context (`ctx.actor`, `ctx.noun`, `ctx.second`, `ctx.action`) and returns one of three verdicts:
  - `star.ok()` — the action succeeds; the function has already applied its effects and emitted its own text.
  - `star.fail(msg)` — the action fails with this message. Equivalent to a failing restriction.
  - `star.pass()` — this rule declines to handle the situation; processing continues to the next rule as though this one hadn't matched. This is the verdict that makes scripted rules composable, and it has no declarative equivalent.
- The function may call `star.try_action{...}` (§7.2.1) and `star.enter_dialogue{...}` (§7.2.2), so redirection and dialogue remain available.
- Phase annotations still apply: `script = @before handle_cursed_take` runs it at the before stage.

**The cost, stated honestly:** a scripted rule is opaque to the tooling. Starbase's rule inspector (§7.4) can show that it runs and in what order, but not what it does; Starforge's conflict report can't reason about it; the static analyser can't tell whether it makes a quest unwinnable. That is an acceptable trade for an escape hatch, but it argues for the editor marking scripted rules visibly, so an author looking at a confusing pipeline can see where the tooling's vision ends.

The same `script`-replaces-everything form should be available for **actions** as well as rules, for the same reason.

---

## 9. The RPG layer

### 9.1 Core mechanism vs. library policy

The notes ask how much of the RPG system is hard-wired. The principle: **the core provides mechanisms that are hard or impossible to build efficiently in a library; the library provides all policy.**

**In the core** (always present, costing nothing when unused):

| Mechanism | Why it can't be library-only |
|---|---|
| **Numeric properties with modifier stacks** — a value = base + Σ(modifiers from equipment, status effects, buffs), with cached recomputation and change notification | Needs to be fast and needs engine-level invalidation. A library implementing this in Lua would recompute on every read. |
| **Named turn-phase insertion points** — the turn loop (§7.1) exposes documented slots (`upkeep`, `initiative`, `action`, `reaction`, `end_of_turn`) that libraries register into | The turn loop is core; it must be extensible without being rewritten. |
| **Timed status effects** — a priority queue of (turn, object, effect) with automatic expiry and serialisation | Needs to be in the save format and the sector catch-up logic. |
| **Seeded RNG with a dice-expression evaluator** (`3d6+2`, `1d20`) | Determinism (§5.5) requires a single RNG. Dice parsing is small and universal. |
| **Equipment slots** — a generic "slot with a type constraint" relation on objects | It is a containment relation; containment is core. |
| **Quest state machine** — quests, stages, objectives, states (unstarted/active/complete/failed), with persistence and a query API | Needs save integration, editor integration, and cross-sector visibility. |
| **Faction/attitude matrix** — a sparse N×N relation with a query API | Wanted by NPC behaviour; cheap; awkward and slow in Lua. |
| **`uses_editor_feature` handling** — the mechanism by which a library declares that Starbase should surface extra UI | Editor-side by nature. |

**In the library** (`stdlib/starscape`, entirely replaceable):

Which stats exist and what they're called. Damage formulas. Hit/miss resolution. Armour semantics. Levelling and XP. Character classes/species/backgrounds. What `weapon` and `armor` mean. Combat verbs and their grammar. Death and its consequences. Encumbrance. Skill checks. Initiative order rules. Loot tables. Crafting. Everything a ruleset designer would argue about.

This split means someone can write a d20 ruleset, a Fudge-dice ruleset, or a narrative stress-dice ruleset alongside Starscape without touching the engine, and Starbase gives all of them a usable editor. It also means Starscape is not privileged: it is the first-party ruleset, not a built-in one, and if it needs an engine change to work then so would everyone else's.

### 9.2 Library metadata and editor features

Per the notes:

```stardata
# stdlib/starscape/library.star
library = {
    id = starscape
    version = "1.0.0"
    display_name = $lib_rpg_name
    requires = { stdlib >= "1.0.0" }
    uses_editor_feature = { rpg quests dialogue }
    # New top-level forms and classes this library contributes
    provides_schema = { stat_block combat_style loot_table }
}
```

`uses_editor_feature` toggles Starbase panels. But — importantly — the *content* of those panels comes from the schema layer (§4.4), not from hard-coded editor forms. `uses_editor_feature = { rpg }` says "show a Stats section in the character inspector and a Combat tab in the ribbon"; the fields inside are generated from the library's own schema declarations. A ruleset with an `insight` stat and no `strength` gets a correct editor with zero editor code. **[DECISION]**

### 9.3 Combat pacing — resolved

Your reading of the model is correct, and it is now the model the engine is built around rather than something layered on top. §7.1's round *is* the d20 round: one round means the PC and every eligible NPC has acted, in combat or out of it. Restated against the six steps you gave:

- Steps 1–3 are three iterations of the §7.1 actor loop (4a–4g), one per combatant, ordered by the ruleset's initiative. Reactions happen at **4g**, nested inside the acting actor's slot, which is what makes "target can react" mean *immediately in response*, rather than "on the target's own turn".
- Step 4's *noticing* happens at 4h; the non-combat NPCs' *acting* happens in their own loop slots (see §7.1 for why I'd merge these, and what it costs).
- Step 5 is ROUND_END.
- Step 6 is the schedule evaluation driven by ADVANCE.

Combat is still not a mode the engine knows about. It is a set of registrations:

```stardata
turn_hook = { id = ss_upkeep      phase = upkeep        priority = 100  script = ss_tick_effects }
turn_hook = { id = ss_initiative  phase = initiative    priority = 100  script = ss_roll_initiative
               conditions = { any_actor = { in_combat == yes } } }
turn_hook = { id = ss_reaction    phase = reaction      priority = 100  script = ss_offer_reactions }
turn_hook = { id = ss_round_end   phase = round_end     priority = 100  script = ss_end_of_round }
```

The `initiative` hook is the important one: absent any hook, the engine's default order is player-first-then-declaration-order, costing nothing. Starscape's hook replaces that order when a combat is live. No engine code knows what initiative *is*.

**Combat granularity** falls out of §5.6 rather than needing its own mechanism: Starscape sets combat actions to `duration = 6`, so a round of combat advances the world clock by six seconds while a round of exploration advances it by a minute. Schedules, effect durations and quest deadlines all keep working across the transition without special-casing, because they are all expressed in ticks.

**Presentation** — the part of [OPEN-5] that was genuinely open. Since one player command produces exactly one round, and a round produces one action per combatant, the output of a combat command is a short, bounded sequence of beats: your attack and its reaction, then each other combatant's. That is a paragraph, not a wall, and it needs no special handling. The frontend can optionally render it into a dedicated combat-log window (§12.3) rather than the main transcript.

The remaining sliver of the question is whether a very long player action (§5.6, `wait an hour`) that overlaps a combat should render every round. It should not: the ruleset should refuse long actions while a combat is live, which is also what every tabletop rule set does.

### 9.4 Quests

```stardata
quest = {
    id = find_the_captain
    title = $q_find_captain_title
    summary = $q_find_captain_summary
    category = main            # main | side | faction | hidden
    # Stages run in declaration order; the journal shows completed ones struck through
    stage = { id = search_bridge
              text = $q_find_captain_s1
              complete_when = { visited = command_bridge } }
    stage = { id = follow_blood_trail
              text = $q_find_captain_s2
              complete_when = { OR = {
                  examined = blood_trail
                  script = { fn = player_asked_about_captain } } } }
    stage = { id = confront
              text = $q_find_captain_s3
              complete_when = { quest_flag = captain_confronted } }
    on_complete = { effects = {
        add = { target = player  prop = xp  amount = 250 }
        set_flag = captain_found
    } }
    on_fail = { conditions = { dead = captain_reyes } }
}
```

Design points:

- **Stages are ordered but not necessarily linear.** A `branches = { ... }` block allows stage graphs; the linear sequence of `stage` entries is the ergonomic common case.
- **`complete_when` uses the same condition language** as everything else (§7.2), evaluated on a change-notification basis rather than polled every turn — the engine subscribes each active quest's predicates to the properties they read, so quest checking costs nothing on turns where nothing relevant changed. This matters: a game with 80 active quests polling every turn is exactly the kind of global cost §5.1 is trying to eliminate.
- **The journal is a first-class output channel** (§12.3), so the frontend can render it in a dockable window and the Glk frontend can render it as a command.
- **Hidden quests** track state without appearing in the journal until revealed — needed for "you have been unknowingly working for the villain" structures.

### 9.5 The party

Your Q8 answer — at least one companion present most of the time, player-controlled in combat, arbitrary switching undecided — moves this from "nice to have" to a core concern, and I want to flag why it is worth getting right now rather than later.

The expensive version of this mistake is an engine that assumes a single protagonist. Everything downstream then quietly hard-codes it: the parser resolves `me` to a global, scope is computed for "the player", `you` in message templates is a literal, saves store one location. Retrofitting a second controllable body means auditing every one of those. The cheap version is to parameterise `actor` everywhere from the first commit — which §7.1 already does — and add the party layer on top.

**The model.** A **party** is an ordered list of actors with one designated **viewpoint**:

```stardata
party = {
    id = player_party
    viewpoint = pc                  # whose senses drive room descriptions and scope
    member = { actor = pc            role = leader }
    member = { actor = companion_kira
               role = companion
               # who decides this member's actions, per §7.1 step 4a
               control = { in_combat = player  out_of_combat = ai }
               # what she does when the AI has the wheel
               follow = { target = viewpoint  distance = same_room } }
    max_size = 4
}
```

Design decisions, each with its reasoning:

- **Viewpoint is separate from control.** Room descriptions, scope (§5.4) and second-person narration follow the viewpoint actor. Control is per-member and per-context. This separation is what lets you have player-controlled companions in combat *without* committing to a switchable viewpoint — which answers your uncertainty about arbitrary switching by making it a later, cheap decision rather than an architectural one. If you eventually want `SWITCH TO KIRA`, it sets `viewpoint` and everything else already works.
- **`control = player` members take their own slot in the initiative loop** (§7.1, 4a) and prompt for input when their slot comes up. The frontend is told whose turn it is via a `Meta` event so it can indicate it. This is the standard cRPG combat feel and needs no new turn machinery.
- **Companions are ordinary actors** — full NPCs with behaviour layers, schedules, knowledge (§10.3) and dialogue. `control = ai` simply means their DECIDE phase consults §10.1 as usual. A companion is not a special class.
- **Following is a goal, not a hack.** `follow` compiles to a standing goal (§10.1 layer 2) at low priority, so it is naturally pre-empted by combat, by interrupts, and by an author-pushed goal, and it resumes afterwards without bookkeeping.
- **Party movement.** When the viewpoint takes an exit, followers get a queued move rather than teleporting. In the common case they arrive in the same round and the narration is one line ("Kira follows you in."). This matters because teleporting companions break percepts, break "who saw what", and break any puzzle involving a locked door.
- **Party-scoped commands.** `KIRA, TAKE THE ROPE` is the traditional parser form and should work. So should addressing the party (`PARTY, WAIT HERE`), which sets goals rather than issuing single actions.
- **Shared inventory is a policy question**, so it lives in the ruleset, not the core. Starscape should default to per-character inventory with a `GIVE`/`TAKE FROM` shortcut, since shared-pool inventory tends to dissolve the fiction of carrying things.

**Impact on §5.4 (scope):** scope is cached per actor, and now several actors need it each round. The cache handles this — it is already keyed by actor — but the party is the reason the design says "per-actor and cached only for actors that need it" rather than "cached for the player".

**Separated party members. [RESOLVED]** Per your call: a party member's sector stays fully active, always. Implementation is the reference-counted pin from §5.3 — every party member holds a pin on their own sector with `reason = party_member_present`, acquired and released automatically as they move. A companion left aboard the ship keeps the ship live, and when she rejoins the party the pin is released and the ship deactivates normally unless something else is holding it.

Your reasoning about perspective switching is the decisive part and worth recording, because it settles the question rather than just answering it: if the viewpoint (§9.5) can ever move to a companion, then that companion's sector must be loaded *anyway* — so a design where separated members go dormant would have to be unwound the moment switching is added. Better to pay the cost from the start than to build a mechanism that a likely future feature invalidates.

The cost is bounded and predictable: at most `max_size` sectors live, so a four-member party that scatters completely holds four sectors. That is a known ceiling an author can design around, unlike the unbounded pin growth that arbitrary `pin_sector` effects could produce. The debugger's pin list (§13.3) shows party pins distinctly from authored ones, so "why are six sectors live" has an immediate answer.

---

## 10. NPC behaviour

The notes call a comprehensive NPC system important, so it gets real architecture rather than a hook.

### 10.1 Four layers

NPC behaviour is decided by consulting four layers in order; the first that yields an action wins.

**1. Interrupts** (highest priority) — event-driven reactions. `alarm_raised`, `attacked`, `player_entered`, `saw_forbidden_thing`. Declared as rules on events.

Libraries may supply **default interrupts** on base classes, so that an NPC the author never explicitly thought about still behaves sanely when something happens near it. Starscape's combat response (§7.1) is the important instance: the goal is that a silent author produces a reasonable NPC, not an oblivious one.

**2. Goals** — a priority stack. Goals are pushed by interrupts, quests, or schedules, and are executed as multi-turn plans. A goal is a small state machine or a Lua coroutine.

```stardata
goal_def = {
    id = investigate
    params = { target urgency }
    # steps run in declaration order; each may take several rounds
    step = { move_to = target  on_blocked = abandon }
    step = { do_action = { action = look } }
    step = { script = evaluate_findings }
    abandon_when = { OR = { in_combat == yes  urgency < 1 } }
}
```

**3. Schedule** — time-of-day driven default behaviour. This is what makes a world feel inhabited, and it is the thing that has to survive being offstage (§5.3).

```stardata
schedule = {
    of_npc = quartermaster_vex
    entry = { from = "06:00" to = "07:00"  location = crew_quarters  activity = idle_morning }
    entry = { from = "07:00" to = "12:00"  location = supply_bay     activity = working }
    entry = { from = "12:00" to = "13:00"  location = mess_hall      activity = eating }
    entry = { from = "13:00" to = "19:00"  location = supply_bay     activity = working }
    entry = { from = "19:00" to = "23:00"  location = crew_lounge    activity = drinking
              conditions = { NOT = { faction_alert = high } } }
    entry = { from = "23:00" to = "06:00"  location = crew_quarters  activity = sleeping }
}
```

Times are calendar times resolved against §5.6, so a game on a 26-hour day needs no changes here.

#### 10.1.3 Schedules offstage: catch-up versus simulation

*(Numbered to match layer 3 above, and to your reference.)*

You put your finger on the real problem with v0.1's catch-up model, and I think the objection is correct and worth taking seriously rather than patching around.

**The flaw:** solving a schedule forward analytically asks "where would Vex be at 14:30?" and answers from the schedule table alone. But schedule entries have `conditions`, and those conditions can depend on what *other* NPCs did. If the entry `drinking in the lounge` is gated on `NOT = { faction_alert = high }`, and the alert would have been raised at 13:00 by a guard who — offstage — found the body, then catch-up puts Vex in the lounge in a world where he should be at his post. The model is only sound when NPCs are independent, and interesting worlds are the ones where they aren't.

**But the opposing intuition is also correct**, and it is a design position rather than a limitation: most players expect the world they left to be roughly the world they return to, changing through plot events rather than through emergent offstage drama. A simulationist world that reshuffles itself while you were away is disorienting more often than it is impressive, and it makes authoring unpredictable — you cannot write a scene if you don't know who will be in the room.

So this is a genuine authorial choice, not a bug, and your instinct to make it a **game-wide switch** is the right resolution. Concretely:

```stardata
# in the project manifest
simulation = {
    offstage_default = catch_up      # none | catch_up | simulate | continuous
    # ceilings for the `simulate` model
    simulate_max_rounds = 5000       # beyond this, fall back to catch_up
    simulate_time_budget_ms = 20000  # ...or beyond this
    simulate_progress = yes          # show a progress indicator (§12.2)
    # baseline NPC reaction to combat breaking out nearby (§7.1)
    default_combat_response = flee
}
```

with per-sector override via the `offstage` block (§5.3). Notes on the `simulate` path:

- **It is a real simulation**, running §7.1's actor loop for that sector's actors with the player absent. NPC actions interact, percepts propagate, conditions are evaluated in sequence. That is the whole point.
- **The player waits, and that is acceptable** — your comparison to cRPG load screens is apt, and a progress indicator is emitted through the frontend protocol so it doesn't look like a hang. The engine reports the elapsed simulation in the debugger so an author can see what it cost.
- **The ceilings matter.** A player who sleeps for a week should not trigger a ten-minute simulation. Past `simulate_max_rounds` or the time budget, the engine falls back to catch-up for the remainder and logs it. Silent unbounded work is worse than a slightly less faithful world.
- **Determinism holds** (§5.5): the simulation runs off the same seeded RNG, so a save taken before the transition and reloaded produces the identical outcome. Without this, `simulate` would make saves feel unreliable, which is a much worse problem than the one it solves.
- **Reporting is suppressed.** Offstage actions emit no text; percepts and world mutations still happen. On return, the author can surface what changed through the knowledge system (§10.3) — Vex tells you about the fight — which is far better narration than a log dump.
- **A middle model is worth considering later**, and I'd note it rather than build it: simulate only NPCs whose schedule entries have conditions that could have been invalidated, catch-up everyone else. It gets most of the correctness for a fraction of the cost. It is also fiddly enough that it should not be in v1.

**Recommendation:** ship `catch_up` as the default and `simulate` as an opt-in, and write the stress corpus (§5.3) with a `simulate` variant so the cost is measured rather than guessed. Your own games can then make the call from data.

**4. Idle** — barks, fidgets, ambient actions from a weighted table, filtered by conditions. Only for NPCs in the player's location; costs nothing otherwise.

### 10.2 Perception

NPCs need to know what happened. The engine emits **percepts** in the AFTER phase (§7.1 step 8): `(event, location, actor, objects, loudness, visibility)`. Each NPC in the location — or within `loudness` radius through the room graph, bounded to the active sector — gets an opportunity to react via its interrupt rules.

This is cheap because it is location-scoped and sector-bounded by construction, and it gives authors the "the guard saw you take the keycard" behaviour that otherwise requires manual plumbing in every action.

### 10.3 Knowledge

**[DECISION]** NPCs get a simple, optional knowledge model: a per-NPC set of known facts (`knows_about(topic)`, `knows_location(object)`, `believes(flag, value)`). Dialogue availability and goal selection can query it; percepts can populate it. This is deliberately much simpler than a full epistemic model — it is a set of tagged booleans — but it is enough for "don't offer dialogue about a murder the NPC hasn't heard about", which is the case that actually comes up.

---

## 11. Dialogue

Per the notes: choice-based trees, cRPG style, replacing `ask/tell`.

### 11.1 Structure

```stardata
dialogue = {
    id = vex_first_meeting
    # Everyone who can speak or be addressed in this conversation.
    # The default speaker is the first participant.
    participant = { actor = quartermaster_vex  role = primary }
    participant = { actor = pc                 role = player }
    participant = { actor = companion_kira     role = companion  optional = yes }

    # Which node to enter on `talk to vex`
    entry = { node = greeting  conditions = { NOT = { met = quartermaster_vex } } }
    entry = { node = greeting_familiar }

    node = {
        id = greeting
        speaker = quartermaster_vex
        text = $vex_greeting
        on_enter = { effects = { set_met = quartermaster_vex } }

        choice = { id = ask_about_station
                   text = $vex_c_station
                   goto = station_info }
        choice = { id = ask_about_captain
                   text = $vex_c_captain
                   conditions = { quest_state = { quest = find_the_captain  state = active } }
                   goto = captain_topic }
        choice = { id = intimidate
                   text = $vex_c_intimidate
                   # a skill check, entirely from the ruleset's vocabulary
                   check = { stat = presence  difficulty = 14 }
                   on_success = { goto = vex_cowed }
                   on_failure = { goto = vex_offended  effects = {
                       adjust_attitude = { npc = quartermaster_vex  amount = -20 } } }
                   # show the player the odds, cRPG-style
                   show_difficulty = yes }
        # A line spoken by a companion rather than the PC — available only
        # when she is present, and it is her voice, not a player choice.
        choice = { id = kira_vouches
                   speaker = companion_kira
                   text = $kira_c_vouch
                   conditions = { present = companion_kira
                                  attitude = { npc = companion_kira  toward = pc  at_least = 40 } }
                   goto = vex_persuaded }
        choice = { id = leave  text = $c_leave  goto = END }
    }

    node = {
        id = station_info
        speaker = quartermaster_vex
        text = $vex_station_info
        # `once` choices grey out or vanish after use
        once = yes
        # An unprompted interjection from a third party, if she is here
        interjection = { speaker = companion_kira
                         text = $kira_i_station
                         conditions = { present = companion_kira
                                        NOT = { said_before = kira_i_station } }
                         chance = 60 }
        goto = greeting            # hub-and-spoke: return to the hub
    }
}
```

Key features:

- **Hub-and-spoke by default.** A node with `goto` back to a hub and `once = yes` gives the standard cRPG shape with almost no ceremony.
- **Conditions on choices** using the universal condition language, so a choice can depend on quest state, knowledge (§10.3), stats, inventory, faction, or a script.
- **Skill checks inline**, with `show_difficulty` for the *Disco Elysium* convention, resolved by the loaded ruleset, absent entirely if none is loaded.
- **Effects on nodes and choices** using the universal effect language.
- **`goto = END`** exits; **`goto = { dialogue = other  node = x }`** jumps across dialogues, letting authors compose shared modules (a `merchant_trade` dialogue reused by every shopkeeper).

### 11.1.1 More than two participants

Adopted as a requirement rather than an option, per your comment — and it is the right call, because two-party dialogue is the thing that makes companions feel like luggage. Three mechanisms, which between them cover what cRPGs actually do:

- **`speaker` on a node** — any participant may deliver a node's line, so a conversation can pass between several NPCs without the author leaving the dialogue.
- **`speaker` on a choice** — the choice is spoken by someone other than the PC. Presented to the player as a selectable option (it is still the player's decision to deploy her) but attributed to and voiced by the companion. This is the *Mass Effect* / *Baldur's Gate 3* companion-intervention pattern, and it does a lot of characterisation work for very little authoring.
- **`interjection` on a node** — an unprompted line from a third party, conditioned and optionally probabilistic, emitted on entering the node before choices are presented. This is what makes a companion feel present in conversations that aren't about her.

Participants may be in **any loaded sector**, not just the current room (§5.3), so a radio conversation with someone aboard the ship is the same construct as a face-to-face one. `present = X` is the condition for co-location; `participating = X` is the weaker one that a remote participant satisfies.

**`speaker = none`** produces a narrator voice, which is what makes the §7.2.2 hybrid pattern read correctly: examining the reactor console starts a "conversation" with no speaker, styled as narration rather than dialogue.

### 11.2 Interaction with the parser

`talk to [someone]` enters dialogue. While in dialogue the input model changes: the frontend presents numbered choices, and the player types a number *or* clicks. The parser is not disabled — typing a normal command exits dialogue with a graceful "You break off the conversation." unless the node is `locked = yes`.

**Barks** — one-line NPC utterances triggered by percepts or idle behaviour — are not dialogue nodes; they're a weighted table on the NPC, printed inline. Keeping them separate stops the dialogue graph from filling with noise.

### 11.3 Presentation

Your model — a dedicated window carrying the most recent line and the response choices, plus an optional portrait window — is a good fit for the frontend protocol and needs one new piece of data and no new mechanism.

**Windows.** Two declared windows (§12.3), both optional and both degradable:

- `dialogue` — shows the current speaker's name, their line, and the numbered choices. It is a *window*, not the transcript, so the conversation doesn't scroll away and the player can re-read the line they are answering.
- `portrait` — shows the current speaker's portrait, changing as the speaker changes. It is driven entirely by whose line is on screen, so a multi-party conversation visibly passes between faces.

**Portraits come from a property on the speaker**, and — per your note about conversing with objects — that property lives on `thing`, not on `person`:

```stardata
class_extension = {
    of_class = thing
    prop_def = {
        portrait       = resource      # image resource path
        portrait_mood  = map<identifier, resource>   # optional variants
        speaker_style  = identifier    # text style for this speaker's lines
        speaker_name   = text          # overrides the object's name in dialogue
    }
}

person = {
    id = quartermaster_vex
    portrait = "res/portraits/vex.png"
    portrait_mood = { angry = "res/portraits/vex_angry.png" }
    speaker_style = speaker_vex
}

# and, for the §7.2.2 hybrid case, an object that "talks"
thing = {
    id = damaged_reactor_console
    portrait = "res/portraits/console_ui.png"
    speaker_name = $reactor_console_name
}
```

A node may request a mood (`portrait_mood = angry`), falling back to the default portrait if the variant is absent — so authors can add moods incrementally without breaking anything.

**Degradation** follows §12.2's capability negotiation, and dialogue is the subsystem where this matters most because the fallbacks are so different:

| Frontend capability | Dialogue presentation |
|---|---|
| Dockable windows + images | Dialogue window + portrait window, as above |
| Windows, no images | Dialogue window with speaker name and style; no portrait |
| Single window (Glk, CLI) | Choices printed inline in the transcript, numbered; speaker name prefixed; `TOPICS` re-prints the current choices |
| Mobile (§12.5) | Dialogue takes the full screen; portrait inline above the line |

**Transcript.** Even with a dialogue window, everything said should also land in the main transcript, so a saved transcript is complete and a player scrolling back can reconstruct the conversation. The dialogue window is a convenience view over the same output events, not a separate channel.

### 11.4 Editing

Dialogue is the subsystem where a text format is most painful and a graphical editor most valuable. Starbase should offer a **node-graph view** with keyboard navigation (arrow keys traverse the graph, Enter edits, `n` creates a child node, `c` creates a choice, `i` creates an interjection), backed by exactly the text format above. The graph view is a *view*, not a separate representation — §13.1.

For multi-party dialogue the graph view should colour nodes by speaker, which turns "does this conversation actually use my companion" into something visible at a glance.

**[OPEN-6], narrowed.** Multi-participant dialogue and portraits are in (§11.1.1, §11.3). The remaining open piece is **voice acting** — audio clips per line, with the attendant lip-sync-free-but-still-fiddly problems of timing, skipping, and the fact that recorded dialogue makes late text edits expensive. My recommendation is to reserve a `voice = resource` property on nodes in the format now, so the data model doesn't need changing later, and implement nothing until there is a game that wants it.

### 11.5 Barks and interjections are not the same thing

I proposed unifying these and you're right that they shouldn't be. The distinction is sharper than I'd credited, and it is structural rather than cosmetic:

|  | Bark | Interjection |
|---|---|---|
| Where it happens | In the transcript, during ordinary play | Inside a dialogue, on entering a node |
| What it changes | Nothing. It is output. | The dialogue's participant set and therefore its subsequent state |
| Afterwards | The speaker is where they were, doing what they were doing | The speaker is **in the conversation** — addressable, and eligible to speak or offer choices at later nodes |
| Can it be responded to? | No | Yes, and that is the point |

Your phrasing is the useful test: an interjection is a character **barging into a dialogue and possibly continuing to be part of it**. A bark is a character saying a thing. Merging them would mean either giving barks a participant model they have no use for, or stripping interjections of the one property that makes them worth having.

So they stay separate, and the interjection gains an explicit consequence:

```stardata
interjection = {
    speaker = companion_kira
    text = $kira_i_station
    conditions = { present = companion_kira
                   NOT = { said_before = kira_i_station } }
    chance = 60
    # the speaker becomes an active participant for the rest of the conversation
    joins = yes            # default
}
```

`joins = yes` promotes a listed-but-passive participant (or, with `participant = { ... optional = yes }`, someone not yet in the conversation at all) into an active one. Nodes reached afterwards can condition on `participating = companion_kira`, address her, and offer her choices. `joins = no` gives a genuine one-off aside — the drive-by version, useful for a character who is present but not really in the scene.

**What they should share is the selection machinery, not the concept.** Both need weighted random choice, condition filtering, once-only tracking, and "don't repeat the last thing you said" suppression. That is one internal mechanism — call it an utterance table — used by both, so authors learn one set of `chance` / `conditions` / `once` semantics and it behaves identically in both places. The commonality is an implementation detail; the two constructs stay distinct in the format and in the editor.

One consequence worth noting for §11.4's graph view: because an interjection changes who is participating, it is a state transition and should be drawn as one. A node that can promote a new speaker looks different from a node that can't.

---

## 12. Runtime and frontends

### 12.1 The split

`starcore` is a library with no UI dependency. It exposes:

```cpp
class Session {
public:
    void  load(VfsPath game);
    void  send_input(std::string_view line);      // or send_choice(int)
    OutputBatch poll();                            // events produced since last poll
    SaveBlob save();
    void  restore(SaveBlob);
};
```

Frontends drive it. This is what makes five frontends tractable — none of them contain game logic.

### 12.2 The frontend protocol

Output is a stream of typed events, serialised as CBOR (or passed as structs in-process):

```
TextSpan     { text, style_id, tooltip_ref?, link_ref? }
LineBreak, ParagraphBreak
WindowOp     { window_id, op: create|destroy|show|hide|resize|set_title }
StatusUpdate { window_id, fields: { key -> value } }
Media        { kind: image|sound|music|video, resource, op: show|play|stop|loop, channel }
InputRequest { kind: line|choice|keypress|confirm, choices?: [...], for_actor? }
JournalUpdate{ quest_id, stage, state }
MapUpdate    { room_id, discovered, exits }
Style        { define style_id -> attributes }
Meta         { turn, clock, score, location_name, active_actor, ... }
Speaker      { actor, display_name, portrait?, style_id }   # §11.3
Progress     { op: begin|update|end, label, fraction? }     # §10.1.3 simulation
```

`InputRequest.for_actor` and `Meta.active_actor` are what let the frontend show whose slot in the initiative loop is being resolved (§7.1, §9.5) — necessary once the player controls more than one body.

`Progress` exists because the `simulate` offstage model (§10.1.3) can take seconds, and a frontend that shows nothing during that is indistinguishable from one that has hung.

Frontends implement what they can and **degrade explicitly**. A frontend declares its capabilities at session start:

```
{ text_styling: rich|basic|none, windows: dockable|fixed|single,
  media: [image, sound, music], tooltips: yes|no, input: [line, choice, mouse],
  progress: yes|no }
```

The core adapts: with `tooltips: no`, tooltip content is emitted as an inline parenthetical or made available via an `examine`-like command; with `windows: single`, status windows become a `status` command; with `media: []`, images fall back to their `alt_text`. Authors write once and get correct behaviour everywhere, which is the only way a multi-frontend story survives contact with real games.

### 12.3 Rich presentation

**Styling** is semantic, not literal: authors mark text as `@style(item_name)` or `@style(danger)`, and a theme maps styles to concrete attributes. This is what lets the same game look right in a Qt window, a browser, and a monochrome Glk terminal.

**Tooltips.** The notes single these out as important for the RPG layer. Mechanism: the text VM's `TOOLTIP_BEGIN <obj>` opcode marks a span as referring to an object. On hover, the frontend sends a `TooltipRequest{obj}` to the core, which runs a `tooltip` template (defaulting, for RPG items, to a stat card) and returns styled content. Lazy, so a screen full of item names costs nothing until hovered.

```stardata
class_extension = {
    of_class = weapon
    tooltip = @style(tooltip_card) "[Name self]\n"
        "@style(stat_line)Damage: [DamageString(self)]\n"
        "@style(stat_line)Range: [self.range]\n"
        "@style(flavour)[self.short_description]"
}
```

**Windows.** Dockable panels declared in data, populated by the core: inventory, character sheet, journal, map, compass, party status, dialogue and portrait (§11.3), combat log (§9.3). Authors can declare their own.

Two notes on the intended layout, given the multi-window vision in the design notes: the desktop default should be a saved, per-game layout the author ships and the player may rearrange, and the layout must survive a window count of zero — every window's content has a command-line equivalent (§12.2), because Glk and mobile will have neither the space nor the docking model.

**Media.** Images, sound, music with channels and crossfade, short video. All resources live in the VFS, so mods can replace them. Video is `@platform`-gated and low priority.

### 12.4 Glk frontend

Worth doing, because it gets us the accessibility work (screen readers, refreshable braille) that the IF interpreter ecosystem has already done, and it gets STAR games running in Gargoyle/Lectrote-style interpreters. Constraints: text-only-ish, fixed windows, no tooltips, no mouse. The capability-negotiation design (§12.2) means this frontend is a few hundred lines, not a port.

### 12.5 Web and mobile

**Web:** `starcore` → Emscripten → WASM; frontend in TypeScript talking the protocol over `postMessage`. This is the distribution channel that matters most for IF (itch.io, IFDB), so it should be a first-class target from early on, not a phase-4 afterthought. Constraint to watch: the sector-streaming design's async loads are natural in a browser and awkward if the core assumes synchronous VFS reads — **so the VFS API must be async-capable from day one**, even though the desktop implementation is synchronous. Retrofitting this later is expensive.

**Mobile:** Qt Quick on iOS/Android. **Q7, resolved: the parser stays.** Your reasoning holds — a permanent verb-and-object palette eats the screen a phone doesn't have, and the result is a worse version of both interaction models. The mobile frontend is a parser frontend with mobile-appropriate affordances rather than a different game:

- Aggressive input completion and a recent-commands strip above the keyboard, which is where a phone can genuinely beat a desktop.
- Tappable nouns in the transcript, inserting the noun into the input line rather than acting directly. Cheap, unambiguous, and it doesn't change the interaction model.
- Dialogue choices are already tap targets (§11.3) and need nothing extra.

**And the honest trade-off, stated rather than engineered around:** the multi-window layout described in §12.3 is a tablet-and-up feature. On a phone, status windows collapse into a pull-up sheet and the portrait window goes away. That is a real degradation, and the capability negotiation in §12.2 is what keeps it from being a per-game authoring problem — the author declares the windows, the frontend decides what it can show. Nobody should spend engineering effort trying to make six dockable panels work on a 6-inch screen.

One consequence worth planning for: **Qt's LGPLv3 and the iOS App Store are a known friction point**, because LGPL requires the recipient be able to relink against a modified library and the App Store's distribution terms make that awkward. Options are a Qt commercial licence, a non-Qt iOS frontend, or Android-only. This should be decided before iOS work starts rather than discovered during submission. Android has no such problem.

---

## 13. Starbase, the editor

### 13.1 The central architectural decision: lossless round-trip

**The text files are the source of truth. The editor is a set of views over them.** Concretely:

- The `stardata` library parses into a **lossless concrete syntax tree** — comments, whitespace, ordering, and original formatting are all preserved as trivia attached to nodes. (The model is rust-analyzer's `rowan`/Roslyn's red-green trees; in C++ this is a green tree of immutable nodes plus a red tree of positioned cursors.)
- Editor operations are **tree edits**, which re-print only the affected span.
- An author's hand-written comments and formatting survive an editor round-trip byte-for-byte outside the edited region.

This is not a nice-to-have. It is the property that makes "graphical editor" and "text format under version control" coexist instead of fighting. Every tool that got this wrong (and most did) forced authors to pick a side.

Consequence: the editor never "saves the project" as a wholesale re-serialise. It applies edits. Diffs are small and reviewable.

### 13.2 Keyboard-first UX

The notes' requirement is that a whole game be writable without the mouse. That requires more than adding shortcuts.

**Command palette** (`Ctrl+P` / `Cmd+P`) as the primary navigation surface: fuzzy-search every object, class, action, rule, quest, dialogue node and file in the project. Type `corr` → jump to the corridor room.

**Command palette, action mode** (`Ctrl+Shift+P`): every editor command is reachable and searchable. No command is mouse-only. This is also the discoverability mechanism — authors learn shortcuts from the palette showing them.

**Modal structural editing in the inspector.** The inspector is a tree of fields; `j`/`k` or arrows move, `Enter` edits, `Tab` moves to the next field, `Esc` backs out. Adding a property, an exit, a rule is a keystroke plus a fuzzy-completed identifier.

**"New object" flow.** `Ctrl+N` → type a class name (fuzzy) → type an id → land in the inspector with required fields queued. Creating a room and wiring its exits should take under ten seconds and zero clicks.

**Bidirectional map editing.** A graph/grid view of rooms and exits where arrow keys move a cursor between rooms and `Shift+Arrow` creates a room in that direction with reciprocal exits wired. This is the single highest-leverage authoring accelerator in ADRIFT and Trizbort, and it should be keyboard-driven.

**Inline text editing** with the interpolation syntax highlighted and function names completed.

**Jump-to-definition / find-references** across the whole project, on any identifier. `F12` and `Shift+F12`.

**Raw text view**, always one keystroke away (`Ctrl+E`), showing the actual `.star` text for whatever is selected, editable in place, with live validation. Power users will live here, and the lossless CST (§13.1) is what makes it safe.

### 13.3 Testing and debugging tools

**Transcript tests.** A test is a file of commands plus expected output patterns:

```
> take lantern
~ Taken.
> north
~ /Corridor/
> examine door
~ /locked/
! assert flag door_examined
```

`starhelm-cli` runs these headlessly. They are the regression suite for a game, and for the stdlib. Starbase records a play session into a transcript test with one command, which is the feature that makes authors actually write tests.

**The debugger.** Break on action, on rule, on property change, on entering a phase. Step through the turn sequence (§7.1) phase by phase and through the actor loop actor by actor, seeing which rules matched and why. Inspect and mutate any object live. Show the live sector set and pin reasons (§5.3), the initiative order, and the cost of the last offstage simulation (§10.1.3). This is the tool that turns "why didn't my rule fire" from an afternoon into a minute, and it should be built early — it is also the engine developers' primary diagnostic.

**Distribution — your suggestion, adopted.** The debugger is `libs/stardebug`, a separate module rather than something compiled into the core:

- **Author's Kit** downloads (Starbase, Starforge, `starhelm-cli`, `starhelm-qt`) include it. **Player** downloads of `starhelm-qt` do not. This keeps the player build smaller, keeps the instrumentation hooks out of the hot path in the build most people run, and avoids shipping a live world-mutation console to people who didn't ask for one.
- A `.spak` manifest may carry `debug_locked = yes`, which an author sets on a release build; a debugger-equipped Starhelm honours it and refuses to attach.
- **Engineering builds ignore the flag**, gated behind a compile-time `STAR_ENGINE_DEV` switch, so a bug report about someone's shipped game is always investigable.

Worth being clear-eyed about one thing: `debug_locked` is **not a security or anti-tamper mechanism** and should never be documented as one. The engine is Apache-licensed (§14.5) and anyone can build a Starhelm that ignores the flag in about four minutes. It is a politeness feature — it stops a player from accidentally spoiling themselves or trivially breaking a puzzle, in the same spirit as a "are you sure?" prompt. Presenting it as protection would set an expectation we cannot meet and would invite exactly the arms race that isn't worth having.

**Static analysis** in the editor: unreachable rooms, objects never referenced, quests that can become unwinnable (reachability analysis over quest predicates), dialogue nodes with no path in, grammar lines that can never match because an earlier line shadows them, rules permanently shadowed by higher-priority overrides.

**Live reload.** Change a message or a rule, and the running game picks it up without losing the play session. Enormously valuable and mostly achievable, given that world *state* and world *definitions* are separate stores. The hard case is a change that invalidates existing state (deleting a class that instantiated objects); handle it by re-running from the transcript, which is fast enough to be invisible.

---

## 14. Packaging, saves and mods

### 14.1 The VFS

A **layered mount stack**, resolved highest-priority-first:

```
  save layer (writable, in-memory + on-disk)     ← highest
  mod layers (ordered by user's load order)
  DLC / expansion layers
  patch layers (shipped updates)
  base game layer (.spak)                        ← lowest
```

Any read resolves through the stack. Any write goes to the save layer. This one mechanism delivers what the notes ask for:

- **Updates** are a patch layer above the base — a patch ships only changed files.
- **Mods** are layers; a mod that wants to change one room ships one file.
- **Saves** are a layer; §14.3.

**Async from day one** (§12.5). The API is `read(path) -> future<bytes>`, with a synchronous fast path when the layer is already resident.

### 14.2 The `.spak` format

A zip archive (so it is inspectable and toolable) containing:

```
  manifest.cbor        # id, version, dependencies, capabilities, entry point
  objects/<sector>.cbor# per-sector compiled object tables
  strings.cbor         # deduplicated string table
  templates.cbor       # compiled text VM programs
  grammar.cbor         # compiled parser trie
  dispatch.cbor        # rule dispatch tables (§7.3)
  scripts/*.lua        # Lua SOURCE, never bytecode (§8.2)
  res/                 # images, audio, fonts
  sig/                 # optional signature block
```

Per-sector object tables mean sector loading is a read of one contiguous blob plus handle fixups — no parsing.

### 14.3 Saves

**Saves are deltas, not snapshots.** The world store already distinguishes class defaults from per-object overrides (§5.2), so a save is: the set of overrides differing from the compiled initial state, plus the RNG state, turn counter, quest states, NPC goal stacks, timed-effect queue, and the input history.

A save for a 50,000-object game where the player has touched 300 objects is a few tens of kilobytes. Autosaves become cheap enough to write every turn.

**Sector serialisation on exit** (the notes' suggestion) falls out of this: leaving a sector with `on_deactivate = serialize` writes that sector's delta into the save layer; re-entering resolves the sector through the VFS, finds the save layer's version on top, and loads it.

**Save compatibility across game updates.** Object identity is by stable string id, not index, in the save's delta records — so a patch that adds objects doesn't invalidate saves. A patch that *deletes* an object the save references produces a warning and drops the record. A save records the game version and the mod set; loading with a different set warns.

### 14.4 Distribution and trust

Your question is the right one to ask, and v0.1 was hand-waving. "Signed" is meaningless without saying signed by whom and trusted how, and the two available models have very different implications.

**The CA model is wrong for this audience.** Windows Authenticode / Apple notarisation assume a paying publisher who can obtain a certificate from a commercial authority. An EV code-signing certificate runs into the hundreds of dollars a year and requires business identity verification. Applied to hobbyists writing text games in their spare time, that is not a security measure, it is a paywall that guarantees approximately nobody signs anything — and a world where 3% of games are signed is worse than one where none are, because signature status stops carrying information.

**The distro model is right**, as you say — author-held keys, published by the author, with no authority in between. Concretely:

- Authors sign a `.spak` with their own key. The detached signature and the public key live in `sig/` inside the archive; the archive's own zip structure is excluded from the signed payload so that adding a signature doesn't invalidate one.
- The public key is published wherever the author likes: their website next to the download, their IFDB entry, their git repo, a keyserver.
- Starhelm validates the signature and reports **who signed it**, not merely *that* it is signed. "Signed by Adrian Welcker <key>" is useful; "Signed ✓" is theatre.

(Which *cryptosystem* implements this is a separate question from the trust model, and the two are easy to conflate. The trust model below is unchanged whether the underlying signature is OpenPGP or raw Ed25519; see the end of this section for why I'd recommend the latter.)

**Trust on first use, with pinning.** Since there is no authority to ask, the runtime does what SSH does:

- First time a key is seen: show the fingerprint, record it against that author identity, proceed.
- Subsequent games from the same author: silently confirm the key matches.
- A **changed** key for a known author is the case that matters — that gets a prominent warning, because it is what a supply-chain attack or a hijacked itch.io account looks like. It is also what a legitimate key rotation looks like, so the message must explain both possibilities rather than crying wolf.
- An unsigned game is not blocked or scary-flagged. It is simply reported as unsigned. Most games will be, and normalising a warning nobody can act on just trains people to dismiss warnings.

**Offline, no infrastructure. [RESOLVED]** Per your call. Sigstore's keyless flow remains a plausible later addition — it removes the biggest failure point of key-based signing, which is that hobbyists lose keys and never rotate them — but it needs a network dependency and a transparency log, and neither is worth it before anyone has asked. Build the verification layer with a pluggable backend and ship one backend.

**On your GnuPG licensing question: the answer is "legally fine, but don't."**

Taking the legal part first, since you asked directly. GnuPG is GPLv3. Invoking `gpg` as a separate process — fork/exec, arguments in, exit code and output back — is *not* linking and does not make Starhelm a derivative work. This is the FSF's own stated position: programs that communicate at arm's length through pipes, sockets or command-line arguments are separate works, and combining them is mere aggregation. So Apache-2.0 Starhelm shelling out to GPLv3 gpg raises no licence-compatibility problem.

But *distributing* gpg alongside it is a separate act with its own obligations. If you ship the binaries, you are a GPLv3 distributor: you must include the licence text and provide corresponding source or a written offer valid for three years, for gpg and for every GPL/LGPL dependency you bundle with it — libgcrypt, libassuan, libgpg-error, npth. That is all doable, and it is also a permanent tax on every release you cut. On Linux, depending on the system `gpg` sidesteps it entirely, because you aren't distributing anything. On Windows and macOS you would be.

**The better answer is not to use OpenPGP at all**, and I'd push back on the choice rather than just the implementation. What we need is: verify that this archive was signed by the key this author published. OpenPGP does that, plus subkeys, key expiry, revocation certificates, a web of trust, an extensible packet format, and thirty years of algorithm agility — roughly 95% of which is machinery we would carry and never use, and some of which is machinery that has produced parsing vulnerabilities.

The modern minimal alternative is **Ed25519 detached signatures in the `signify`/`minisign` style**:

- Verification is one function call against **libsodium** (ISC licence) or **monocypher** (CC0/BSD-2). No GPL, no bundling obligations, no subprocess, no output parsing.
- A signature is 64 bytes. A public key is 32 bytes — **short enough to paste into an itch.io page or an IFDB entry as a single line of text**, which directly serves the distribution flow you described. A PGP fingerprint is 40 hex characters that nobody transcribes correctly and everybody copies from the wrong place.
- The format is simple enough to describe completely in a page of the spec, which matters for a system where a third party might want to write their own tooling.
- Signing tooling ships in the Author's Kit as a Starforge subcommand (`starforge sign`), so authors never touch a separate program.

The cost is that an author with an existing PGP identity can't reuse it. That is a real loss for maybe a handful of people, and it can be addressed later by adding an OpenPGP *verification* backend via **rnp** (BSD-2-Clause, the library Thunderbird uses) — which, notably, would also avoid the GPL question.

**Recommendation:** Ed25519/minisign-style as the shipping backend, pluggable trust layer, OpenPGP verification via rnp only if someone actually wants it. If you'd rather stay with PGP, shelling out to system gpg on Linux and bundling on Windows/macOS is legitimate — just budget for the source-offer obligation in the release process.

**And the load-bearing point, restated:** the sandbox (§8.2) is the actual defence. Signing establishes *provenance* — this is the file that author published — and provenance is worth having. It does not establish *safety*, and the documentation must not blur the two, because a player who believes a signature means the game is safe is worse off than one who knows the sandbox is what's protecting them.

A `capabilities` block in the manifest declares anything unusual (a raised memory ceiling, a `continuous` offstage sector, an unusually long simulation budget), shown to the player on request.

### 14.5 Licensing

Not legal advice — I'm not a lawyer, and the trademark and RPG-licensing points below in particular are worth ten minutes with someone who is if the Starscape name matters to you. But your plan is well-constructed and mostly right, and there are three places I'd adjust it.

**Your plan, as I understand it:**

| Component | Your proposal | Verdict |
|---|---|---|
| Engine, editor, runtime | Apache 2.0 | Good choice |
| Standard library | CC0 / public domain | Right intent, wrong instrument — see below |
| Starscape RPG library (code) | Apache 2.0 | Fine, with a caveat |
| Starscape SRD (the rules as text) | OGL or ORC | Right idea; ORC over OGL |

**Apache 2.0 for the engine — agreed, and for a reason worth naming.** Apache 2.0 carries an explicit patent grant and a patent-retaliation clause, which MIT and BSD do not. For a project that might one day attract a contributor with a patent portfolio, that is free insurance. It is also compatible with Qt's LGPLv3 when Qt is dynamically linked (§2), and with GPLv3 downstream — though notably *not* with GPLv2-only, which is a real if unlikely constraint on what a third party could combine it with.

**On CC0 for the standard library — right goal, and I'd change the instrument.** The intent is exactly right: authors will copy chunks of the stdlib into their own games, and making them think about attribution for a snippet of a container definition is friction with no upside. But CC0 is a poor fit for *code* specifically, and this is not a hypothetical objection — Fedora stopped accepting CC0-licensed code in 2022 on precisely this ground. The problem is CC0 §4(a), which expressly does **not** waive patent rights, so CC0 code carries an unresolved patent question that a permissive code licence would have settled.

The instruments that achieve your actual goal without that defect are **MIT-0** (MIT No Attribution) or **BSD-0-Clause**. Both are OSI-approved, both are three sentences long, and both say "do whatever you want, no attribution required" — which is what you meant by CC0. **Recommendation: MIT-0 for `stdlib/`.**

**On the Starscape split — the caveat.** Licensing the *implementation* Apache 2.0 while licensing the *SRD* under ORC is coherent, but be aware of what it means in practice: the Apache-licensed code contains the rules. Anyone may read `stdlib/starscape/`, understand the system completely, and reimplement or extend it without ever accepting the SRD's licence — and Apache 2.0 gives them that right irrevocably. The SRD licence therefore governs people who want to *republish the rules text*, not people who want to *use the rules*. That is probably what you want, but it is worth knowing that the code licence is the permissive one and it wins.

Also note that game *mechanics* are not copyrightable in the US in the first place (only their specific expression is), which is the legal fact the entire OGL edifice was built to route around. The rules of Starscape are, as a matter of law, free for anyone to use regardless of what any licence says. What licences and Product Identity mechanisms actually protect is names, text and setting.

**On reserving "Starscape" — the one place I'd push back slightly.** You wrote that the name would remain reserved "even without trying for a trademark". Partly true, and the qualification matters:

- **ORC's** "Reserved Material" and **OGL 1.0a's** "Product Identity" mechanisms both let you designate names — Starscape, your setting, your characters — as excluded from the licence. This works, and it is the standard tool.
- **But it binds only licensees.** It is a contract term, enforceable against people who accepted the licence in order to use your SRD. Someone who never took the licence is not bound by it, and against them your only recourse is common-law trademark rights arising from actual use in commerce, which are real but narrow and geographically limited.
- In practice this is almost certainly enough: the people plausibly tempted to publish "Starscape" material are exactly the people using your SRD, and they are bound. But "reserved" is doing lighter work than it sounds like.

**ORC over OGL 1.0a**, for what I'd expect is an uncontroversial reason: in January 2023 Wizards attempted to deauthorise OGL 1.0a, and although they retreated, the episode demonstrated that a licence the grantor claims it can revoke is not a stable foundation. The community's response was the ORC License — irrevocable by construction, stewarded independently of any single publisher. Post-2023, publishing new material under OGL 1.0a signals either unawareness of that history or indifference to it. **Recommendation: ORC.** (CC-BY-4.0 is the other post-2023 option, and it is *simpler*, but it has no Reserved Material mechanism, so it would not help you keep the Starscape name.)

**"Built on STAR IF technology"** — a good idea, and it should be a **trademark usage guideline document**, not a licence term. Keeping it out of the licence means it stays a courtesy that people follow because it's nice, rather than an obligation that makes the licence non-standard and gives lawyers something to object to. Apache 2.0 §6 already withholds trademark rights, so the STAR IF name is not granted away by the code licence; a short `TRADEMARKS.md` saying "here is how we'd like you to refer to the system, and here is what we ask you not to do" covers it. (And the *Built on NT Technology* nod is good. Windows 2000's splash screen is an oddly fond memory for something so beige.)

**Summary of what I'd recommend:**

| Component | Licence |
|---|---|
| `libs/`, `apps/` — engine, editor, runtime, compiler | Apache 2.0 |
| `stdlib` — standard library | **MIT-0** (not CC0) |
| `stdlib/starscape` — RPG library implementation | Apache 2.0 |
| Starscape SRD — the rules as a document | **ORC License** (not OGL 1.0a), with Starscape and setting names as Reserved Material |
| Documentation and author manual | CC-BY-4.0 |
| The names STAR IF, Starbase, Starforge, Starhelm, Starscape | Not licensed; `TRADEMARKS.md` usage guideline |

---

## 15. Roadmap

Phases are sized so each ends with something demonstrable. Estimates assume one experienced developer full-time; scale accordingly.

### Phase 0 — Foundations (6–8 weeks — see note)
Repo, CMake presets, CI on all three desktop platforms. `stardata`: lexer, lossless CST, parser, writer, schema layer, error reporting with spans. `starvfs` with directory and zip layers, async API. Catch2 harness.
**Exit:** `tests/corpus/tour.star` parses, round-trips byte-identically, and reports good errors on a corpus of broken files.

**Broken down in `docs/phase-0-backlog.md`** — 46 tasks with dependencies and acceptance criteria. That exercise puts the honest figure at **13 weeks**, not 6–8: the CST and the schema layer are a fortnight each, and this estimate treated diagnostics as a printf rather than as infrastructure. Deferring combination modes, suggestions, reference resolution and the VFS to Phase 1–2 — none of which the exit criterion requires — brings it back to 8–10 weeks. The backlog recommends taking both deferrals and treating Phase 0 as strictly "the format is real and tooled".

### Phase 1 — Minimal playable core (12–14 weeks)
World store, containment, sectors (single sector only). Parser with grammar trie and scope. The full turn sequence of §7.1 including the actor loop and the named hook points — **this is built once, now, rather than retrofitted**, since §7.1's whole argument is that bolting an actor loop onto a player-only pipeline later is the mistake Inform authors have to work around. Rules with phase annotations, `try_action`, scripted rules. The tick clock and action durations (§5.6). Text VM, and `starlang` with the English rule table and adaptive-text substitutions (§4.10) — built here rather than later, because every stdlib message depends on it and retrofitting agreement into written messages is worse than writing them against it. Lua sandbox. `starhelm-cli`. A `stdlib` with ~20 verbs.
**Exit:** *Cloak of Darkness* (the IF community's canonical minimal game) is playable start to finish from source, and its transcript test passes in CI. Separately, a two-actor scratch test demonstrates the initiative loop.

### Phase 2 — The differentiator (8–10 weeks)
Multi-sector loading/unloading, stubs, pinned sectors, offstage catch-up and `simulate`. Rule dispatch indexing. Incremental scope caching, including the presence relation and two-sided doors (§5.7) — doors are the cross-sector transition trigger, so they belong here rather than in Phase 1. Save deltas and undo snapshots. The stress corpus and its performance CI gates, in both `catch_up` and `simulate` variants.
**Exit:** the 20,000-room stress game holds under 5 ms p99 turn time, and turn time is flat across world sizes. **This is the go/no-go gate for the whole premise.**

### Phase 3 — Starbase v1 (12–16 weeks)
Qt Widgets shell. Schema-driven inspector. Outline/project tree. Command palette (both modes). Map view with keyboard editing. Raw-text view. Jump-to-definition. Live reload. Transcript recording. The debugger as an author's-kit plugin (§13.3). The latency and stability budgets of §1.4 enforced in CI from the first commit of this phase, not retrofitted.
**Exit:** a small game is authored end-to-end without the mouse, and the interaction-latency budget holds on the stress corpus.

### Phase 4 — Rich runtime (8–10 weeks)
Frontend protocol, capability negotiation. `starhelm-qt` with Qt Quick: styling, dockable windows, tooltips, images and audio. Starforge and `.spak`. Mods and patch layers. Signature verification (§14.4).
**Exit:** a distributable, signed `.spak` that runs with full presentation.

### Phase 5 — NPCs, dialogue and party (12–16 weeks)
Percepts, interrupts, goals, schedules, knowledge. Multi-participant dialogue: format, runtime, interjections, portraits, the dialogue and portrait windows, and the Starbase node-graph editor. Barks. The party layer (§9.5) including player-controlled members and follow goals.
**Exit:** a demo sector with six scheduled NPCs whose routines are observable, a companion who interjects in conversations and follows correctly through a sector transition, and dialogue that reacts to world state.

### Phase 6 — Starscape (12–16 weeks)
Core mechanisms (modifier stacks, turn hooks, status effects, dice, equipment slots, factions, quest state machine). The `stdlib/starscape` ruleset: initiative, combat resolution, reactions, stats, equipment. Starbase RPG panels via `uses_editor_feature`. Journal, character sheet and combat log windows. Player-controlled party members in combat.
**Exit:** a playable combat-and-quest vertical slice with a companion the player directs in combat.

### Phase 7 — Reach (ongoing)
Web frontend (WASM + TS). Glk frontend. Mobile. Author documentation. The Starscape SRD. Public release.

Localisation belongs here, and is the point at which Unicode Inflection (§4.10.4) is re-evaluated as a `starlang` backend — by then it may have Windows support and tagged releases. If it does not, a second language pack written against the existing selector is the fallback, which is why §4.10.3 insists the mechanism is selection rather than generation.

**Total to a credible public release: roughly 17–22 months** of full-time single-developer work — revised up from v0.1's 14–18, because Phases 1, 5 and 6 all grew: the actor loop moved into Phase 1, multi-party dialogue and the party layer are now requirements rather than options, and Starscape is a fuller ruleset than the reference implementation I originally scoped. If this is evenings-and-weekends rather than full-time, multiply accordingly and take §16.1's burnout risk seriously.

Phases 3, 5 and 6 are the ones that historically balloon.

### 15.1 Ordering, revised

v0.1 argued for pulling community-facing work earlier — web frontend before the editor — on the theory that distribution attracts feedback. **§1.3 says that argument is wrong for this project.** You are the primary user; the editor is the thing you are actually building; a text-only workflow with a browser export would be optimising for an audience that doesn't exist yet at the expense of the one that does.

**Keep the order as listed.** The one adjustment I would still make: pull the **Glk frontend** forward into Phase 4. It is cheap (a few hundred lines, given §12.2), it forces the capability-negotiation design to be exercised by a genuinely impoverished frontend before three richer ones have baked assumptions into it, and it means every game is accessible to screen-reader users from the first release rather than as a later accommodation.

The other thing worth pulling forward is writing **a second sample game**, in parallel with Phase 2 rather than after it (§16.1). One sample game validates that the format works; two validate that it generalises, and the difference between those is where format designs usually fail.

---

## 16. Risks and open questions

### 16.1 Risks

| Risk | Severity | Mitigation |
|---|---|---|
| **The performance premise doesn't hold.** Real games' costs turn out to be dominated by something the architecture doesn't address. | Critical | Build the stress corpus in Phase 0, not Phase 2. Profile against it continuously. Phase 2 is an explicit go/no-go. |
| **Scope creep.** RPG + NPC + dialogue + editor + five frontends is four projects. | Critical | The phase gates are real gates. Anything not needed for the current phase's exit criterion is deferred, written down, and not built. |
| **The format's expressiveness ceiling.** Authors hit a wall where the data vocabulary can't say what they mean and everything becomes Lua. | High | Write two complete sample games in Phase 1–2, not one. The stdlib being written in the format (§2.2) is the canary. |
| **Editor/format round-trip fidelity** degrades under real use, and authors stop trusting the editor. | High | Property-based testing: generate random edits, assert round-trip invariants. Make this a CI gate from Phase 0. |
| **Solo-project burnout** over a 17–22 month build, on a hobby schedule. | High | Every phase exit is a thing that *works* and is enjoyable to use — this is the main defence, and it is why the phases are ordered as they are. Play your own games on the engine as early as Phase 1. Accept that the schedule is elastic and the project is not going anywhere. |
| **Nobody adopts it.** Inform 7 and TADS have thirty years of momentum. | Medium | Deliberately downgraded from High: §1.3 makes adoption a secondary goal. Optimising for it would compromise the primary one. Your own games are the argument, and if they're good the tool follows. |
| **The party and multi-party dialogue features are half-built** when Phase 5 runs long, leaving companions that don't quite work. | Medium | These are the features most likely to be cut under pressure and the most damaging to cut halfway. Better to ship a companion who only follows and interjects than one with a broken combat-control path. Sequence Phase 5 so each mechanism is complete before the next starts. |
| **Lua sandbox escape.** | Medium | The measures in §8.2 are individually well-understood; the discipline is never loading bytecode and never expanding the whitelist casually. Consider a security review before public distribution. |
| **Qt licensing.** LGPL requires dynamic linking and relinking rights; static linking needs a commercial licence, and iOS App Store distribution is a known friction point (§12.5). | Medium | Dynamic-link Qt and document it. Decide the iOS question before starting iOS work, not during submission. Android is unaffected. |
| **The `simulate` offstage model turns out to be unusably slow or produces incoherent worlds** in practice (§10.1.3). | Medium | It is opt-in with `catch_up` as the default, so the failure mode is "a feature nobody uses" rather than "a broken game". Measure it on the stress corpus in Phase 2 before committing to it in a real game. |

### 16.2 Questions closed in v0.2

| # | Question | Resolution |
|---|---|---|
| 1 | Runtime name | **Starhelm**. Starscape is the first-party ruleset and game series (§1.5). |
| 2 | Trademark check | Not doing one. Check for package-name collisions at publish time only. |
| 3 | Does a failed action consume a turn? | Per-action `advances_turn`, game-level default; redirected actions cost one round total (§7.1, §7.2.1). |
| 4 | `[]` vs `{}` for ordered blocks | Distinction dropped entirely; everything is ordered (§4.2). |
| 5 | Combat pacing | d20 round semantics, built into the turn sequence rather than layered on (§7.1, §9.3). |
| 6 | Dialogue scope | Multi-participant and portraits are in; voice deferred with a reserved property (§11.1.1, §11.3, §11.4). |
| 7 | Mobile input | Parser stays; multi-window is a tablet-and-up feature, acknowledged as a trade-off (§12.5). |
| 8 | Party | Yes — companions, player-controlled in combat, viewpoint separated from control so switching stays a cheap later decision (§9.5). |
| 9 | Time model | Tick-based clock, per-action durations, author-defined calendars; stdlib defaults to one minute per action (§5.6). |
| 10 | Undo | Stays. Snapshot-based on the save-delta representation, configurable depth (§16.5). |
| 11 | Licensing | Apache 2.0 / MIT-0 / ORC, with three adjustments to your plan (§14.5). |
| 12 | Prior-art review | Yes — scoped in §16.6. |

### 16.3 Closed in v0.3

| Question | Resolution |
|---|---|
| Party separation across sectors | Members pin their own sector, always. Perspective switching would require it anyway, so the alternative would have to be unwound later. (§9.5) |
| Per-sector clocks | One universal clock — the stardate — with relativity handwaved. Local calendars and offsets survive for *display* and schedule authoring only. (§5.6) |
| Signing backend | Offline, no infrastructure, no transparency log. Cryptosystem recommendation revised — see below. (§14.4) |
| Barks vs. interjections | Distinct constructs; an interjection changes the participant set, a bark changes nothing. Shared selection machinery only. (§11.5) |
| Mutable initiative order | Yes. `remove` and `insert` are mandatory anyway; `defer` and `ready` are a few dozen lines on top. (§7.1.1) |

### 16.4 Still open

Three judgement calls remain, none of which block any phase:

1. **Signing cryptosystem** (§14.4). You said PGP; I'd push for Ed25519/minisign-style instead, and the reasoning is in §14.4 — a 32-byte key an author can paste into an itch.io page, one ISC-licensed function call to verify, none of GnuPG's GPLv3 redistribution obligations on Windows and macOS, and none of OpenPGP's unused 95%. The trust model you approved is unchanged either way. Worth a decision, not urgent.
2. **The baseline `combat_response`** (§7.1) — `flee` or `observe`? `flee` is safer and duller; `observe` keeps bystanders present as witnesses and reads better in a confined setting like a station corridor, where fleeing may not even be possible. Settle by playing a scene, not by arguing.
3. **Whether Starscape exposes `defer` and `ready`** as player commands (§7.1.1). The core builds them regardless; this is a ruleset design question that wants a table's worth of play behind it.

### 16.5 On undo, since you raised the RNG point

Your reasoning is right and worth recording, because it settles a design question that usually generates more heat than this.

Undo is snapshot-based, taken at the ADVANCE phase (§7.1), keyed on the turn counter, with configurable depth (default 32 rounds). Snapshots are cheap because they are save deltas (§14.3) — a snapshot is the diff against the previous one, so the memory cost is proportional to how much the player changed, not to world size.

And your observation about the RNG is correct and is a real property of the design, not an accident: since all randomness comes from a single seeded generator whose state is snapshotted with everything else (§5.5), undoing a combat round and repeating the *same* action reproduces the *same* result exactly. Rerolling requires doing something genuinely different first. That is a much better outcome than any anti-savescum policy could achieve, and it costs nothing because determinism was already required for saves and replay testing.

Two details:

- **Undo unwinds a whole round**, not a single actor's slot. Undoing "just the enemy's attack" would be incoherent.
- **Multi-level undo across a sector transition** needs the outgoing sector's serialised state kept in the undo chain rather than discarded (§5.3). Worth noting now because it is easy to write the transition code in a way that makes this impossible.

### 16.6 The prior-art review

Worth doing, and I'd scope it narrowly to be useful rather than encyclopaedic: how Inform 7, TADS 3, ADRIFT 5, Dialog and Kerkerkruip specifically handle (a) turn-sequence extensibility, (b) rule ordering and conflict resolution, (c) scope computation, and (d) NPC scheduling — the four places where this proposal makes its strongest claims. The point would be to find out where each of them tried something like this and retreated, and why.

Kerkerkruip in particular deserves a close read given §7.1, since it is the most serious attempt to build RPG turn semantics on a system that didn't want them, and its scar tissue is the most directly informative.

I'd hold this until after the format spec is settled — it is a research task that could otherwise expand indefinitely, and the decisions it would inform most are already made.

---

## 17. Immediate next steps

The design questions are settled. What remains before Phase 0 is writing things down, not deciding them.

1. ~~**Write `docs/stardata-spec.md`** and promote `star-if-example.txt` into `tests/corpus/tour.star`.~~ **Done**, along with `tests/corpus/invalid/` and `tests/check_stardata.py`. Review target: Appendix A of the spec, which lists the thirteen decisions the specification made that this proposal had left open.
2. **Build the stress-corpus generator** before any engine code, so the performance premise is measurable from the first commit, and build it with a `simulate` variant so §10.1.3's cost is a number rather than a guess.
3. **Write `TRADEMARKS.md` and add licence files** (§14.5) — a five-minute job that gets much harder after the first outside contribution.
4. **Wire CI**: `python3 tests/check_stardata.py --self-test --strict` on every push, so the spec and corpus are held together from the first commit rather than from whenever the parser lands.

The three items in §16.4 can be decided whenever they come up: the signing cryptosystem before Phase 4, the `combat_response` baseline and the `defer`/`ready` commands from play during Phase 6.

Everything else in Phase 0 is conventional work that can proceed once the format is nailed down.
