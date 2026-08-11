# Stardata Format Specification

**Version:** 0.1 (draft) · **Date:** 2026-08-11 · **Status:** Normative
**Companion documents:** `docs/proposal.md` (rationale), `tests/corpus/tour.star` (conformance corpus)

---

## 1. Scope

This document specifies **Stardata**, the source data format of the STAR IF System. It defines the lexical structure, grammar, and static semantics of `.star` files, together with the schema layer, object model, text template language, and the condition and effect vocabularies built on them.

It does not specify runtime behaviour beyond what is needed to give constructs meaning. Turn sequencing, scope computation, sector streaming and the frontend protocol are specified in `docs/proposal.md` and, in due course, in `docs/runtime-spec.md`.

### 1.1 Requirement levels

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT** and **MAY** are to be interpreted as in RFC 2119.

### 1.2 Conformance levels

An implementation may conform at three levels. Each subsumes the previous.

| Level | Name | Requirement |
|---|---|---|
| 1 | **Syntactic** | Parses §3–§5 correctly. Round-trips any conforming file byte-for-byte through its concrete syntax tree (§14.2). |
| 2 | **Schematic** | Level 1, plus validation against the schema layer (§7) with the diagnostics of §15. |
| 3 | **Semantic** | Level 2, plus the object model (§8), text templates (§9), and the condition and effect vocabularies (§10, §11). |

A **conforming file** is one that a Level 2 implementation accepts without error.

`tests/corpus/tour.star` is the reference conformance corpus. A Level 2 implementation MUST accept it; a Level 1 implementation MUST parse and round-trip it.

`tests/corpus/invalid/` holds negative fixtures: files that are invalid on purpose, each declaring the diagnostic code it must provoke. A rule in this document that has no corresponding fixture is a rule nobody has confirmed can actually fail.

`tests/check_stardata.py` implements Level 1 plus the subset of Level 3 that does not require schemas, and runs both corpora in CI — together with every fenced example in this document, since spec examples are read far more often than the corpus and are where drift hides. It is a stand-in until `libs/stardata` exists and should be deleted then, not maintained beside it.

### 1.3 Relationship to the Clausewitz format

Stardata is derived from the Paradox/Clausewitz data format and is deliberately *not* compatible with it. The differences are all restrictions or additions made to remove ambiguity:

- Comparison operators are lexically distinct from assignment (§3.6).
- A block contains either bare scalars or statements, never both (§5.2).
- Values are typed, and types are declared (§6, §7).
- Annotations (`@before`, `@priority(3)`, …) are part of the grammar (§5.4).
- Localisation keys (`$key`) are a distinct scalar kind (§3.5).

### 1.4 Deliberate omissions

Three capabilities that comparable systems provide are excluded by design. Each is recorded here so that "can Stardata do X" has an answer, and so that a later decision to add one is a deliberate reversal rather than a drift.

#### 1.4.1 No dynamic object creation

There is no `new`. Every object that can exist is declared in the source and exists from load. TADS 3 permits `new Thing()`; Inform, Dialog and ADRIFT do not, and this specification follows them.

The reasons are architectural rather than stylistic, and all of them are consequences of decisions already made:

- **Saves are deltas against a compiled initial state** (proposal §14.3), keyed by stable string id. An object with no compiled counterpart needs a parallel "created objects" section in every save, its own id-generation scheme, and a reconciliation story for what happens when a patch changes the class it was created from.
- **The object store is structure-of-arrays, sized at load** (proposal §5.2). Growth means reallocation and handle invalidation on a data structure whose whole point is stable, cheap handles.
- **Sector residency is a static property** (proposal §5.3). A dynamically created object belongs to no sector's object table, so there is no answer to where it serialises.
- **Static analysis assumes a closed world.** Unreachable-object detection, quest-winnability analysis and the rule dispatch index (proposal §7.3) are all built at compile time over a known object set.

The sanctioned pattern for "an apparently unlimited supply" is a **pool**: declare two or three objects, keep the inactive ones in an off-stage container, and move them into play as needed. Fiction absorbs this easily — a match dispenser that will not let you hold more than one match at a time needs no further excuse. Whether the standard library should provide a `pool` form to save authors hand-rolling the bookkeeping is an open convenience question, not a change to this rule.

#### 1.4.2 No general table construct

Inform's tables are a powerful relational store. They are not provided, because the uses that motivate them are already covered by typed, editable, purpose-built forms: dialogue (§Appendix C.3), schedules, goal definitions, quest stages, loot tables, and `map<K,V>` properties.

A general table would invite authors to rebuild those systems inside it, badly and without editor support. If a genuine gap appears, the intended answer is a new typed form or a `map<K,V>` property — not a relational store.

#### 1.4.3 No arbitrary control flow in data

Effect blocks (§11) have no conditionals and no loops. This is what makes them renderable as an editor form and analysable by the compiler. Anything needing control flow uses a script (§12).

---

## 2. Files and encoding

1. A Stardata source file MUST be encoded in **UTF-8**.
2. A leading byte-order mark (U+FEFF) MUST be accepted and MUST be preserved by a round-tripping implementation. It MUST NOT be treated as content.
3. Line terminators MAY be LF or CRLF. An implementation MUST accept both, MUST NOT normalise them in a round-trip, and MUST treat them identically for all other purposes.
4. The conventional file extension is `.star`. Locale files conventionally use `.star` under a `loc/` directory; nothing in the format depends on this.
5. A file MAY be empty. An empty file is a conforming file containing zero statements.

---

## 3. Lexical structure

The lexer produces a stream of tokens and *trivia*. Trivia — whitespace and comments — carries no meaning but MUST be preserved by a round-tripping implementation (§14.2).

### 3.1 Whitespace

Whitespace is any run of U+0009 (tab), U+000A (LF), U+000D (CR), or U+0020 (space). Whitespace separates tokens and is otherwise insignificant. In particular, **newlines have no syntactic significance**: a block written across ten lines and the same block written on one line are identical.

Other Unicode whitespace (U+00A0, U+2028, …) MUST be rejected with a diagnostic suggesting the ASCII equivalent, rather than silently accepted. This catches a common class of copy-paste error.

### 3.2 Comments

```
Comment ::= '#' <any characters up to but excluding the next line terminator>
```

There is no block-comment form. A `#` inside a string literal is an ordinary character.

To comment out a region, an author uses a leading `#` per line; Starbase provides this as a single command.

### 3.3 Identifiers

```
Identifier ::= [A-Za-z_] [A-Za-z0-9_.]*
```

Identifiers are **case-sensitive**. The dot is permitted so that namespaced identifiers (`starscape.combat.melee`) are single tokens; it has no built-in meaning at the lexical level.

An identifier MUST NOT be one of the reserved words in §3.8.

### 3.4 Numbers

```
Integer ::= '-'? [0-9]+
Decimal ::= '-'? [0-9]+ '.' [0-9]+
```

- `Integer` is a signed 64-bit value. Out-of-range literals MUST be rejected.
- `Decimal` denotes a **fixed-point** value with exactly three fractional digits, stored as a scaled 64-bit integer. A literal with more than three fractional digits MUST be rejected rather than rounded, because silent rounding of a damage formula is precisely the kind of error this choice exists to prevent.
- A literal with a leading `.` (`.5`) MUST be rejected; write `0.5`.
- A trailing `.` (`5.`) MUST be rejected; write `5.0` or `5`.

A `Decimal` literal MAY be assigned to a property of type `float` (§6.2), in which case it is converted to IEEE-754 double at load time. This is the only context in which floating-point arithmetic enters the system.

### 3.5 Strings and localisation keys

```
String ::= '"' ( StringChar | Escape )* '"'
StringChar ::= <any character except '"' , '\' , or a line terminator>
Escape ::= '\' ( '"' | '\' | 'n' | 't' | '[' | ']' | '$' | '@' | 'u' HexQuad )
HexQuad ::= [0-9A-Fa-f]{4}
```

- A string literal MUST NOT span a line terminator. Long text is composed with adjacent-literal concatenation (§3.5.1).
- `\u` followed by four hex digits denotes a Unicode scalar value. Surrogate halves MUST be rejected; supplementary characters are written directly in UTF-8 or via two escapes only if they form a valid pair — implementations SHOULD simply recommend writing the character directly.
- `\[`, `\]`, `\$` and `\@` produce literal `[`, `]`, `$` and `@` in text that would otherwise be interpreted by the template language (§9).

```
LocKey ::= '$' Identifier
```

A `LocKey` is a reference into the localisation table (§9.6). It is a distinct scalar kind, not a string, and MUST be accepted anywhere a `text`-typed value is expected.

#### 3.5.1 Adjacent-literal concatenation

Two or more string literals separated only by trivia are concatenated into a single string, with no separator inserted:

```stardata
long_description = "The reactor housing is scorched black. "
                   "Something went very wrong here, and recently."
```

This is the sanctioned way to write text longer than a line. An implementation MUST preserve the split points in its concrete syntax tree so that a round-trip reproduces the original formatting.

### 3.6 Operators

```
Op ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-=' | '?='
```

| Operator | Name | Valid in |
|---|---|---|
| `=` | bind | any context |
| `==` | equals | condition context only |
| `!=` | not equals | condition context only |
| `<` `>` `<=` `>=` | comparison | condition context only |
| `+=` | extend | binding context, collection-typed keys only |
| `-=` | reduce | binding context, collection-typed keys only |
| `?=` | bind-if-unset | binding context only |

**Context** is determined by the schema (§7): a key whose declared type is `condition_block` establishes a condition context for its contents, recursively, until a key of some other type is reached.

A bare `=` used in a condition context where a comparison was clearly intended (that is, where the right-hand side is a scalar and the key names a property) MUST produce a **warning**, not an error, suggesting `==`. The format stays forgiving of the Clausewitz habit while steering away from it.

Conversely, `==` used in a binding context MUST be an error.

### 3.7 Punctuation

```
Punctuation ::= '{' | '}' | '(' | ')' | ',' | '<' | '>'
```

`{` and `}` delimit blocks (§5). The remainder are used only in the three bracketed constructs that appear in value positions: annotation arguments (§3.8), type expressions (§4.2) and calls (§4.3).

`<` and `>` are also comparison operators (§3.6). The two uses never collide, and §4.2 gives the disambiguation rule.

`[` and `]` are **not** punctuation. They occur only inside string literals, where they belong to the template language (§9) and to parser grammar lines. Outside a string literal they MUST be rejected. This is deliberate: it keeps them permanently available to the template language and guards against the block-syntax use that was considered and rejected during design.

### 3.8 Annotations

```
Annotation     ::= '@' Identifier ( '(' AnnotationArgs ')' )?
AnnotationArgs ::= AnnotationArg ( ',' AnnotationArg )*
AnnotationArg  ::= Identifier | Integer
```

Annotations attach to a value (§5.4). The defined set is in §5.4.1; an unknown annotation MUST be an error, since silently ignoring one changes behaviour invisibly.

### 3.9 Reserved words

The following identifiers are reserved and MUST NOT be used as an identifier in any position where a user-chosen name is expected:

```
yes  no  inherit  none  all  and  or  not  if  else  end  true  false
```

`true` and `false` are reserved but not valid values; an implementation MUST reject them with a diagnostic pointing at `yes` / `no`. This exists solely to give the many authors arriving from other formats a good error message.

`AND`, `OR`, `NOT`, `COUNT_AT_LEAST` and similar combinators in the condition language (§10.3) are written in upper case and are *keys*, not reserved words; the reservation above is of the lower-case forms.

---

## 4. Grammar

```ebnf
File      ::= Statement*

Statement ::= Key Op Value

Key       ::= Identifier | String

Op        ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-=' | '?='

Value     ::= Annotation* ( Block | TypeExpr | Call | Scalar )

Scalar    ::= Identifier | Integer | Decimal | String | LocKey
            | 'yes' | 'no' | 'inherit' | 'none'

TypeExpr  ::= Identifier '<' TypeArg ( ',' TypeArg )* '>'
TypeArg   ::= TypeExpr | Identifier

Call      ::= Identifier '(' ( CallArg ( ',' CallArg )* )? ')'
CallArg   ::= Call | Scalar

Block     ::= '{' BlockBody '}'
BlockBody ::= Statement* | Scalar*
```

That is the entire grammar. Everything else in this document constrains what a *conforming* file may contain, but no further syntax is introduced.

A `Key` written as a `String` (`"pick up" = { ... }`) is permitted so that keys needing characters outside the identifier set can be expressed. It is rare; the schema decides whether a given key position accepts it.

### 4.1 Adjacent string literals

Two or more `String` tokens separated only by trivia form a single `Scalar` (§3.5.1). This is handled at the token level, so it applies uniformly wherever a string may occur.

### 4.2 Type expressions and the `<` disambiguation

`TypeExpr` is how the generic types of §6.2 are written: `list<int>`, `set<identifier>`, `map<identifier, int>`, `ref<thing>`, `enum<condition_enum>`, `flags<damage_type_enum>`, `block<rule>`, `map<direction, ref<room>>`.

`<` is both a comparison operator and a type-argument opener, but the two are unambiguous with one token of lookahead:

> A `<` occurring in a **value** position — that is, immediately following an identifier that itself follows an operator, or an identifier inside a type argument list — opens a type argument list. A `<` occurring in an **operator** position, between a key and a value, is the comparison operator.

The two can never be confused, because a `Statement` requires exactly one `Op` between its `Key` and its `Value`, and a `Value` is never followed by an operator. `strength < 14` has `<` in the operator slot; `type = list<int>` has it inside the value.

A parser MUST NOT depend on whitespace to make this distinction, since whitespace is insignificant (§3.1).

**Shorthand.** In a type position, a bare identifier naming a declared `enum` is shorthand for `enum<that>`. So `condition = condition_enum` and `condition = enum<condition_enum>` are equivalent inside a `prop_def` block. The shorthand is the common form; the explicit form is available for clarity.

### 4.3 Calls in value positions

`Call` permits a template-language expression (§9.2) where the schema's declared type accepts one — chiefly the object-valued fields of effects (§11.5):

```stardata
try_action = {
    action = remove_from
    noun   = noun
    second = HolderOf(noun)
}
```

The call is evaluated in the enclosing context, with the same slot bindings a template would receive. A call in a position whose declared type does not accept an expression MUST be an error.

Note that a `Call` and a `TypeExpr` are distinguished purely by their bracket, so no ambiguity arises between them.

---

## 5. Block semantics

### 5.1 Order is significant

**Block contents are ordered.** The loader MUST preserve source order; the compiler MUST preserve it into the compiled form; every engine API that iterates a block MUST iterate in that order.

This is unconditional. There is no unordered block form. Where order carries no semantics — the values of an enum, the traits of a class — nothing is lost by guaranteeing it; where it does — an effects list, a dialogue's nodes, a schedule's entries — it is guaranteed rather than incidental. It also satisfies the engine-wide determinism requirement without introducing a syntactic category that claims to be unordered while the engine orders it anyway.

A consequence for tooling: an editor MUST NOT silently reorder a block. Sorting is an explicit, author-invoked operation producing a visible change.

### 5.2 List blocks and record blocks

A block is one of two shapes, determined by its contents:

- A **list block** contains only bare scalars: `values = { breathable toxic vacuum }`
- A **record block** contains only statements: `exits = { north = corridor }`

A block containing both a bare scalar and a statement MUST be rejected. The diagnostic SHOULD identify the minority form and suggest the correction, since the usual cause is a missed `=`.

An empty block `{ }` is neither shape and is legal in both positions. Its meaning is context-dependent: for a collection-typed key it denotes the empty collection; for a rule sub-block it denotes an explicit override with no content (§5.4.2).

### 5.3 Duplicate keys

Whether a key may appear more than once in a block is declared by the schema (§7.2) via its `arity`.

- `arity = one` (the default): a second **binding** occurrence MUST be an error, and the diagnostic MUST cite both spans.
- `arity = many`: repeated occurrences are collected, **in source order**, into a sequence.

Arity counts **binding** occurrences only — those using `=` or `?=`. The modifier operators `+=` and `-=` do not bind; they transform whatever value is in effect, and any number of them may follow a binding, or stand alone and transform an inherited value. They apply in source order:

```stardata
synonyms  = { thing object }      # binds
synonyms += { item }              # -> { thing object item }
synonyms -= { object }            # -> { thing item }
```

This is legal for a key whose `arity` is `one`, because only the first statement is a binding.

Multi-arity keys are how the format expresses ordered sub-collections without a separate list syntax:

```stardata
quest = {
    id = find_the_captain
    stage = { id = search_bridge  ... }
    stage = { id = follow_trail   ... }
    stage = { id = confront       ... }
}
```

The three `stage` entries form an ordered sequence. Common multi-arity keys include `stage`, `node`, `choice`, `step`, `entry`, `member`, `participant`, `interjection`, `prop_def`, `rule`, `key`, `month`, and the condition combinators `NOT`, `OR`, `AND`.

### 5.4 Annotations on values

An annotation modifies how a value combines with an inherited or otherwise pre-existing value of the same key. Annotations attach to the *value*, after the operator:

```stardata
restrictions = @before { ... }
successMsg   = @before "You reach a gloved hand into the embers."
effects      = @override { ... }
tooltip      = @style(tooltip_card) "..."
```

Multiple annotations MAY be applied; they are processed left to right. Contradictory combinations (`@before @after`) MUST be rejected.

#### 5.4.1 Defined annotations

| Annotation | Applies to | Meaning |
|---|---|---|
| `@before` | block, string | This value's contents apply before the inherited ones |
| `@after` | block, string | …after |
| `@override` | block, string, scalar | …instead of |
| `@merge` | block | Merge into the inherited block key by key |
| `@remove` | block | Remove the listed entries from the inherited collection |
| `@priority(n)` | block | Ordering tiebreak within a phase; `n` is an integer, default 0, higher runs first |
| `@debug` | any | Present only in development builds; the compiler strips it from a release build |
| `@platform(id, …)` | any | Present only on the listed frontends (`qt`, `web`, `glk`, `cli`, `mobile`) |
| `@style(id)` | string | The string's default text style (§9.3) |

`@debug` and `@platform` are **conditional presence** annotations: they do not modify combination, they determine whether the statement exists at all in a given build. A statement removed by `@debug` or `@platform` MUST behave exactly as though it had not been written, including for arity checking.

#### 5.4.2 Default combination

When no annotation is present, the default depends on the key's declared `combine` mode in the schema (§7.2):

| `combine` | Default with no annotation |
|---|---|
| `override` | `@override` |
| `merge` | `@merge` |
| `append` | `@after` |
| `smart` | `@override` if the block is empty, otherwise `@after` |

The standard library's rule sub-blocks are declared as follows, reproducing the behaviour specified in the proposal:

| Key | `combine` | Effective default |
|---|---|---|
| `restrictions` | `smart` | empty block overrides; non-empty appends |
| `conditions` | `smart` | as above |
| `effects` | `override` | replaces the action's effects |
| `successMsg` | `override` | replaces the action's message |
| `prop_def` | `merge` | adds to the inherited property set |

### 5.5 `inherit` and `none`

- `inherit` as a value means "do not modify what was inherited here". It is exactly equivalent to omitting the statement, and exists so an author can make the intent explicit and so a graphical editor has something to write.
- `none` as a value means "explicitly empty / absent", and is distinct from both `inherit` and an unset property. Assigning `none` to a `ref<C>`-typed property clears it; assigning `none` to a collection clears it.

---

## 6. Values and types

### 6.1 Scalar kinds

Six lexical scalar kinds exist: `Identifier`, `Integer`, `Decimal`, `String`, `LocKey`, and the keywords `yes` / `no` / `inherit` / `none`. The schema maps each key to a **declared type**, and the loader coerces the lexical kind to it, or reports a type error.

### 6.2 Declared types

| Type | Accepts | Notes |
|---|---|---|
| `bool` | `yes`, `no` | |
| `int` | `Integer` | signed 64-bit |
| `decimal` | `Decimal`, `Integer` | fixed-point, three fractional digits |
| `float` | `Decimal`, `Integer` | IEEE-754 double; excluded from save state; for cosmetic values only |
| `text` | `String`, `LocKey` | localisable and interpolatable (§9) |
| `string` | `String` | raw, never localised or interpolated; for machine-facing values |
| `identifier` | `Identifier` | a bare symbol with no reference semantics |
| `ref<C>` | `Identifier`, `none` | a reference to an object of class `C` or a subclass; validated at compile time |
| `enum<E>` | `Identifier` | a value declared by `enum = { id = E … }` |
| `flags<E>` | list block of `Identifier` | a bitset over `E`; membership tests are O(1) |
| `list<T>` | list block, or record block if `T` is a block type | ordered, duplicates permitted |
| `set<T>` | list block | ordered, duplicates rejected, O(1) membership |
| `map<K,V>` | record block | ordered by declaration |
| `script` | `Identifier` | names a Starscript function (§12) |
| `resource` | `String` | a VFS path to an asset; existence checked at compile time |
| `clock_time` | `String` of the form `"HH:MM"` or `"HH:MM:SS"` | resolved against the sector's calendar to ticks (§11.6) |
| `duration` | `Integer`, or `default` | in ticks |
| `dice` | `String` of the form `"3d6+2"` | parsed at compile time |
| `condition_block` | record block | establishes a condition context (§10) |
| `effect_block` | record block | establishes an effect context (§11) |
| `text_or_script` | `String`, `LocKey`, `Identifier` | a message, or the name of a function producing one |

### 6.3 Collection operators

For a key whose type is `list<T>`, `set<T>`, `map<K,V>` or `flags<E>`:

```stardata
synonyms  = { lantern lamp }      # replaces
synonyms += { brass_lamp }        # extends, preserving existing order
synonyms -= { lamp }              # removes the named entries
```

`+=` appends to the end. `-=` removes by value; removing a value that is not present MUST produce a warning, not an error, because a library update that already removed it should not break a game.

`?=` binds only if the key is currently unset, at any inheritance level. It is intended for library files that want to offer a default that a game may pre-empt regardless of load order.

### 6.4 Globals and constants

Not all author data belongs to an object. A station-wide alert level, a count of how many times the player has been caught, the identity of whoever the player last accused — none of these is a property of any particular thing.

```stardata
global = {
    id      = alert_level
    type    = enum<alert_level_enum>
    initial = none
    doc     = $g_alert_level_doc
}

global = { id = times_caught     type = int          initial = 0 }
global = { id = captain_found    type = bool         initial = no }
global = { id = last_accused     type = ref<person>  initial = none }
global = { id = seen_endings     type = set<identifier>  initial = { } }

const = { id = max_reactor_temp  type = int  value = 1200 }
```

- A `global` is **mutable and saved**. It is part of world state and appears in save deltas (proposal §14.3), in undo snapshots, and in the determinism guarantees of §14.1.
- A `const` is **immutable and not saved**. It exists so that tuning values have a name and a place; the compiler may inline it.
- Both are typed, using the same types as properties (§6.2), including collections.
- Both MUST be declared. There is no implicit creation.
- Ids live in a single namespace. Libraries SHOULD prefix theirs (`starscape.combat_round`), which the dotted identifier form of §3.3 supports.

Read in conditions with `global`, write in effects with `set_global` and `add_global`:

```stardata
conditions = { global = { alert_level == high } }
effects    = { set_global = { id = alert_level  value = high }
               add_global = { id = times_caught  amount = 1 } }
```

#### 6.4.1 Flags are boolean globals

The `set_flag` / `clear_flag` effects and the `flag_set` condition are **sugar over a declared `bool` global**, not a separate store.

This matters more than it looks. As undeclared magic strings, `set_flag = captain_found` paired with `flag_set = captain_finded` is a silent, permanent bug of exactly the kind the schema layer exists to prevent — the condition simply never fires, and nothing reports it. Requiring the global to be declared makes the typo a compile error.

```stardata
global   = { id = captain_found  type = bool  initial = no }
effects  = { set_flag = captain_found }        # ≡ set_global = { id = captain_found value = yes }
conditions = { flag_set = captain_found }      # ≡ global = { captain_found == yes }
```

An implementation MUST reject `set_flag`, `clear_flag` or `flag_set` naming an undeclared global, or one whose type is not `bool`.

### 6.5 Collections at runtime

Collection-typed properties and globals (`list<T>`, `set<T>`, `map<K,V>`, `flags<E>`) are mutable during play.

**Conditions** (§10.4). Only the two that are genuinely boolean live here; anything that *computes a value* goes through `compare` (§10.6):

| Predicate | Form |
|---|---|
| `contains` | `{ contains = { collection = seen_endings  value = good_end } }` |
| `is_empty` | `{ is_empty = seen_endings }` |

**Effects** (§11.1):

| Effect | Meaning |
|---|---|
| `list_add` | append, or insert at `index` |
| `list_remove` | remove by value, or by `index` |
| `list_clear` | empty the collection |
| `map_put` · `map_remove` | by key |

```stardata
effects = {
    list_add    = { collection = seen_endings  value = good_end }
    map_put     = { collection = npc_moods  key = quartermaster_vex  value = hostile }
    list_remove = { collection = waypoints  index = 0 }
}
```

Three constraints follow from decisions already made:

1. **Iteration order is defined** (§14.1). `list` preserves insertion order; `set` and `map` preserve declaration-then-insertion order. There is no unordered collection.
2. **There is no iteration in effect blocks** (§1.4.3). Walking a collection is a script operation. The declarative vocabulary covers add, remove, clear and test, which is what most authoring needs.
3. **Mutated collections are save state.** A collection whose contents differ from the compiled initial value is stored as a property override (proposal §5.2), which is the same mechanism as any other changed property.

An out-of-range `index` is a runtime error, not a silent no-op, for the reasons in §8.8.4.

---

## 7. The schema layer

### 7.1 Purpose

Every top-level form and every key within it is described by a **schema**. Schemas are themselves written in Stardata, in `stdlib/core/schema.star`, and libraries MAY contribute more. The schema layer serves three purposes simultaneously, and this is the main reason it exists rather than the validation being hard-coded:

1. **Validation** with precise, span-accurate diagnostics.
2. **Editor generation** — Starbase renders an inspector for any object by walking its schema, so a library that adds a form gets an editor for free.
3. **Documentation and completion** from a single source.

### 7.2 Schema declarations

```stardata
schema = {
    id = action
    doc = $schema_action_doc
    # An `action` is a top-level form (as opposed to a nested block shape)
    top_level = yes

    key = { name = id           type = identifier      required = yes  unique_in = action }
    key = { name = match        type = list<string>    required = yes }
    key = { name = restrictions type = condition_block combine = smart }
    key = { name = effects      type = effect_block    combine = override }
    key = { name = successMsg   type = text_or_script  combine = override }
    key = { name = failureMsg   type = text_or_script  combine = override
            doc  = $schema_action_failuremsg_doc }
    key = { name = duration     type = duration        default = default }
    key = { name = advances_turn type = enum<advances_turn_enum>  default = on_success }
    key = { name = rule         type = block<rule>     arity = many }
}
```

Fields of a `key` declaration:

| Field | Type | Meaning |
|---|---|---|
| `name` | identifier | the key |
| `type` | type expression (§6.2) | declared type |
| `required` | bool | default `no` |
| `arity` | `one` \| `many` | default `one` (§5.3) |
| `default` | scalar | value if absent |
| `combine` | `override` \| `merge` \| `append` \| `smart` | §5.4.2; default `override` |
| `unique_in` | identifier | the namespace within which the value must be unique |
| `doc` | text | documentation string |
| `editor` | identifier | a hint for the inspector widget (`text_area`, `object_picker`, `slider`, …) |
| `deprecated` | text | if present, using this key produces a warning carrying this message |

`block<S>` as a type means "a record block conforming to schema `S`", which is how nested shapes such as `rule`, `stage`, `node` and `choice` are declared.

### 7.3 Open and closed schemas

A schema is **closed** by default: an unknown key MUST be an error, with a "did you mean …?" suggestion computed by edit distance against the declared keys.

A schema MAY declare `open = yes`, permitting unknown keys, which are retained and made available to scripts. This is intended for author-defined metadata and SHOULD be rare.

### 7.4 Object instantiation forms

A statement whose key is the id of a declared **class** instantiates an object of that class:

```stardata
room = { id = your_cell  exits = { north = corridor } }
```

The class name is on the left and the object's id inside. This is deliberate and MUST NOT be reversed: it is what allows the schema layer to dispatch on the left-hand key, it groups a file by kind for scanning and outlining, and it makes every top-level statement uniform in shape.

The keys permitted inside an instantiation block are those of the class's property set (§8), plus the universal keys `id`, `traits`, `in`, `on`, `part_of`, and `sector`.

---

## 8. The object model

### 8.1 Classes

```stardata
class = {
    id = container
    of_class = thing
    prop_def = {
        capacity = int
        holding_relation = enum<relation_enum>
    }
    capacity = 10
    holding_relation = in
}
```

- A class has exactly **one** parent, named by `of_class`. Single inheritance.
- `prop_def` declares properties. A `prop_def` block is a record block mapping property name to type.
- Any other key sets the class's **default value** for that property. The property MUST be declared, by this class or an ancestor.
- The root class is `entity`. `thing`, `room`, `person` and `direction` derive from it and are supplied by `stdlib/core`.

### 8.2 Class extension

```stardata
class_extension = {
    of_class = room
    prop_def = { condition = condition_enum }
    condition = breathable
}
```

`class_extension` modifies an existing class in place: it adds properties and changes defaults for a class declared elsewhere, including in a library the author cannot edit. It MUST NOT change `of_class`.

Extensions apply in load order (§13.2). Two extensions setting the same default is legal; the later wins, and an implementation SHOULD report it at `--verbose` since it is occasionally a surprise.

### 8.3 Traits

Traits are named bundles of properties, defaults and rules that classes and individual objects mix in. They exist because single inheritance cannot express orthogonal capabilities — openable, lockable, lit, wearable, edible — that cut across the class tree.

```stardata
trait = {
    id = openable
    prop_def = {
        open = bool
        openable_by_hand = bool
    }
    open = no
    openable_by_hand = yes

    rule = {
        of_action = open
        conditions = { noun = { has_trait = openable } }
        restrictions = { noun = { open == no  failureMsg = $already_open } }
        effects = { set = { target = noun  prop = open  value = yes } }
        successMsg = $opened_default
    }
}

class = { id = door  of_class = thing  traits = { openable lockable } }
thing = { id = ornate_box  of_class = container  traits = { openable lockable } }
```

- Traits MAY be mixed in by a class (inherited by all its instances and subclasses) or by an individual object.
- A trait MUST NOT declare `of_class` and MUST NOT participate in the class hierarchy.
- **Conflict is an error, not a resolution order.** If two traits mixed into the same class or object declare the same property name, that MUST be rejected unless the mixing declaration resolves it explicitly:

```stardata
class = {
    id = trick_chest
    of_class = container
    traits = { openable trapped }
    # `resolve` maps each contested property to the trait it comes from.
    resolve = { open = openable }
}
```

There is no implicit method-resolution order. The cost is occasional explicit resolution; the benefit is that no behaviour is ever selected by a rule the author does not know.

- The first 64 traits declared in a project are represented as a bitmask, making `has_trait` a single machine instruction. Beyond 64, a spillover set is used; the compiler SHOULD report when a project crosses the threshold, since it is a small performance cliff worth knowing about.

### 8.4 Objects and property resolution

Looking up property `p` on object `o` proceeds in this order, stopping at the first hit:

1. `o`'s own `prop_def` declarations (§8.7) and slots
2. traits mixed into `o` directly, in declaration order
3. `o`'s class, then its ancestors, each checking the class's own defaults then its traits, in declaration order
4. if nothing matches: see §8.8

Storage stores only *overrides*: an object whose `condition` matches its class default consumes no space for it. This is also the representation the save-delta system uses.

### 8.5 Containment

Every object has at most one parent, and the parent link carries a **relation**:

```
in · on · under · behind · carried · worn · part_of
```

An object's initial placement is written with the relation as the key:

```stardata
thing = { id = brass_key   in  = ornate_box }
thing = { id = tarnished_mug  on  = mess_table }
thing = { id = access_panel   part_of = reactor_console }
```

`part_of` differs from the others in that the child is destroyed with the parent and is in scope whenever the parent is.

Containment cycles MUST be rejected at compile time.

### 8.6 Presence — being in more than one place

Containment gives an object exactly one parent. Some objects are in several places at once: the sky, the ground, an ambient sound, and above all a door, which is referable from both of the rooms it joins.

**Presence is a second relation, orthogonal to containment.** An object is *contained* by at most one parent and *present in* any number of rooms. Presence does not move the object, does not mutate the containment tree, and is not itself world state that changes as the player travels.

```stardata
backdrop = {
    id = the_sky
    traits = { scenery fixed_in_place }
    present_in = { antecourt observation_deck }
}
```

`present_in` accepts either a list block of room ids, or a query:

```stardata
present_in = { where = { in_sector = station_alpha }  dynamic = no }
```

A query is resolved at compile time into a concrete room set unless `dynamic = yes`, in which case its predicate is evaluated when scope is computed. `dynamic` MUST be opted into explicitly, because it moves work from build time to every turn.

#### 8.6.1 Facets — presenting differently from each side

An object present in several rooms MAY vary its presentation by observer location. A `side` entry declares presence in one room together with the facet used when the object is observed from it:

```stardata
door = {
    id     = airlock_hatch
    traits = { openable lockable fixed_in_place }
    open   = no                     # shared state: one property, one record
    locked = yes

    side = { room = airlock         direction = out
             name = $hatch_inner    description = $hatch_inner_desc }
    side = { room = docking_gantry  direction = in
             name = $hatch_outer }
}
```

- `side` is sugar over `present_in`: it contributes its `room` to the presence set.
- Any facet field MAY be omitted; it then falls back to the object's own property.
- **State is not a facet.** Properties such as `open` and `locked` are declared on the object and stored once. A `side` block MUST NOT set a property that is not a declared facet field, so the two sides of a door cannot desynchronise.
- Facet fields are `room`, `direction`, `name`, `description`, `synonyms`, `article`.

An `exits` entry MAY name an object with `side` entries instead of a room. The engine resolves through it to the room named by the other side:

```stardata
room = { id = airlock  exits = { out = airlock_hatch } }
```

#### 8.6.2 Requirements

1. An object with `present_in` or `side` MUST NOT be portable. There is no sensible answer to where it went. This MUST be a compile error.
2. An object with `side` entries MUST have at least two, and their `room` values MUST be distinct.
3. A `side` block MUST NOT assign any key outside the facet field list.
4. Presence and containment MAY both be absent, but an object with neither is unreachable and SHOULD be reported.
5. A multi-located object is **resident whenever any room it is present in is loaded**, and belongs to no single sector's object table. An implementation MUST place such objects in a shared table (§14.2 of the proposal). Cross-sector doors depend on this.
6. Presence MUST NOT emit movement events, percepts, or save deltas. It is not a mutation.

#### 8.6.3 What presence is not for

An object needing *different state* in each location is not one object. Several objects sharing a class is the correct model, and an implementation SHOULD NOT extend facets to cover mutable per-room state.

### 8.7 Properties declared on a single object

In practice most custom properties are one-offs belonging to exactly one object, and requiring a class for each of them is the wrong tax. This is a real difference between an IF language and a general-purpose one: in C++ or Java a field must belong to a type, and a type with one instance is an accepted cost. Here it is not, because a game is mostly singular things.

An object instantiation MAY therefore carry its own `prop_def`:

```stardata
thing = {
    id       = reactor_console
    in       = control_room
    prop_def = {
        times_rebooted   = int
        diagnostic_code  = string
    }
    times_rebooted  = 0
    diagnostic_code = "E-114"
}
```

- The property exists on **this object only**. It is not added to the class and no sibling gains it.
- It is otherwise an ordinary property: typed, saved, readable in conditions, writable in effects, and rendered in Starbase's inspector.
- Property resolution (§8.4) checks an object's own `prop_def` first.
- A local `prop_def` MUST NOT redeclare a name the object already inherits, with a different type. Redeclaring with the *same* type is redundant and SHOULD be reported as such.

**Why a declaration is still required.** It would be possible to let an unrecognised key simply create a property, and some systems do. One line of `prop_def` instead buys: typo detection (`times_reboofed` is an error rather than a second property), a type for the compiler and the save format, an editor widget, and a name that survives refactoring. The cost is a line; the alternative reintroduces exactly the untyped looseness §1.3 exists to remove.

Starbase SHOULD offer "add a property to this object" as a first-class inspector action, so the declaration is generated rather than typed.

### 8.8 Reading a property the object may not have

`noun.damage` is fine when `noun` is a weapon and meaningless when it is a sandwich. The compiler knows what properties exist *somewhere* in the program, but not which object a slot will hold at run time. This section specifies what happens.

#### 8.8.1 The static type of a slot

Every slot has a static type, and most of them are narrower than "some object":

| Slot | Static type |
|---|---|
| `self` | the declaring class or object |
| `actor` | `person`, or whatever the ruleset narrows it to |
| `noun`, `second` | determined by the action's grammar token (proposal §6.2) |

Grammar tokens already carry this information: `[something]` yields `thing`, `[someone]` yields the `animate` trait, and `[class:weapon]` yields `weapon`. An action written with a typed token therefore gets its narrowing for free, which is a good reason to prefer `[class:weapon]` over `[something]` where it applies.

#### 8.8.2 Three static answers

For a read of property `P` on a slot of static type `T`, the compiler distinguishes:

- **Definitely present** — `T` or an ancestor or trait of `T` declares `P`. Compile to a direct access.
- **Definitely absent** — nothing in the program that could satisfy `T` declares `P`. **Error.** This is the case that catches typos, and it is the common one.
- **Possibly present** — some objects satisfying `T` declare `P` and others do not. The author must resolve it; see below.

#### 8.8.3 Narrowing — the decision

**[DECISION]** Both of the options you posed, layered, with static narrowing as the primary mechanism and a runtime escape that must be written out.

A "possibly present" read MUST be justified by one of:

**1. A narrowing condition earlier in the same conjunction.** Because the condition language is ordered and short-circuiting (§10.1), `of_class`, `has_trait` and `is` narrow the slot's static type for everything after them, and a rule's `when` and `conditions` narrow its `restrictions`, `effects` and messages:

```stardata
rule = {
    of_action = examine
    when      = { noun = { of_class = weapon } }      # narrows noun to weapon
    successMsg = "It is rated for [noun.damage] damage."   # now legal
}
```

**2. An explicit `has_prop` test**, which is both a runtime check and a narrowing operator:

```stardata
restrictions = {
    noun = { has_prop = damage
             damage > 3
             failureMsg = $too_feeble }
}
```

**3. A defaulting read**, where absence is genuinely acceptable. Because this computes a value rather than testing one, it goes through `compare` (§10.6):

```stardata
conditions = {
    compare = {
        prop_or = { obj = noun  prop = damage  default = 0 }
        value > 3
    }
}
```

Narrowing does **not** survive an `OR` branch, since only one branch is known to have held. Narrowing established inside a `NOT` does not escape it.

If none of the three applies, the read is a compile error naming the property, the slot's static type, and the classes that do declare it, with a fix-it offering `has_prop`.

**Why not runtime-only.** Deferring the whole question to run time would move a large class of authoring error out of the build and into play, which is precisely the trade the schema layer exists to refuse. **Why not static-only.** Narrowing cannot reach into scripts, where the slot is a dynamic Lua value, and there are honest cases — a property some subclasses add — where the check genuinely belongs at run time. The layering keeps the compile-time guarantee where it is achievable and makes the runtime case visible in the source.

#### 8.8.4 Runtime behaviour

Where a check reaches run time — from a script (§12), through `has_prop`, or through a collection index — reading an absent property is a **catchable error**, never a silent default.

```lua
local d = noun.damage          -- raises if noun has no `damage`
local d = noun:get("damage", 0) -- explicit default, never raises
```

The error unwinds to the turn boundary like any other script error (proposal §8.2) and is reported with the object, the property, and the source location. Silent defaults are excluded deliberately: a `0` that should have been an error produces a game that is subtly wrong rather than obviously broken, and subtly wrong is much harder to find.

**"Absent" means undeclared for this object.** A declared property always has a value — its own, its class default, or its type's zero — so an unset property is never absent. Absence is a question about the type, not about the value.

### 8.9 Replication — deferred

**Status: specified, not scheduled.** Nothing implements this before Phase 4, and it is recorded here because it constrains the parser, which is Phase 1.

Inform's "In the lab are ten clones" instantiates a kind several times. The equivalent:

```stardata
person = {
    id     = clone
    count  = 10
    in     = lab
}
```

- Generates objects with ids `clone_1` … `clone_10`. Ids MUST be deterministic and stable across builds, since saves key on them (proposal §14.3).
- All copies are identical; there is no per-copy variation. Anything needing variation is several declarations.

**The constraint on Phase 1.** Ten identical clones make disambiguation absurd: asking "which do you mean, the clone or the clone?" is worse than guessing. The parser MUST therefore resolve between candidates that are **indistinguishable by name and by every property the player can observe** by choosing one deterministically and silently, rather than prompting. That behaviour is needed whether or not replication is ever implemented — two hand-declared identical coins raise the same problem — so it belongs in the parser from the start. See proposal §6.4.

---

## 9. Text and templates

A value of type `text` is a **template**: a program that produces styled output when evaluated. Templates are compiled by Starforge into a small stack machine; nothing in this section requires an author to know that.

### 9.1 Template grammar

```ebnf
Template  ::= Fragment*
Fragment  ::= Literal | Interp | StyleDir | Conditional
Interp    ::= '[' Expr ']'
StyleDir  ::= '@style' '(' Identifier ')' | '@endstyle'
Conditional ::= '[if' Expr ']' Template ( '[else]' Template )? '[end]'
```

Literal text is everything not otherwise matched. `\[`, `\]` and `\@` escape the delimiters.

### 9.2 Expressions

```ebnf
Expr      ::= Apply | Call | Path | Slot | Literal
Call      ::= Identifier '(' ( Expr ( ',' Expr )* )? ')'
Apply     ::= Identifier Expr
Path      ::= Slot ( '.' Identifier )+
Slot      ::= 'actor' | 'noun' | 'second' | 'self' | 'player' | 'speaker' | Identifier
```

Slots are bound by the evaluation context: an action's message binds `actor`, `noun` and `second`; an object's own `description` binds `self`; a dialogue node binds `speaker`.

A function named in a template MUST resolve at compile time to either a **template builtin** (evaluated by the stack machine, never entering Lua) or a Starscript function (§12). Unresolvable names MUST be a compile error.

#### 9.2.1 Juxtaposition is single-argument application

`Apply` — a function name followed by a single expression, with no parentheses — is sugar for calling that function with that one argument:

```
[the noun]        ≡  [the(noun)]
[a noun]          ≡  [a(noun)]
[plural noun]     ≡  [plural(noun)]
[the HolderOf(noun)]  ≡  [the(HolderOf(noun))]
```

This is a **general rule, not a special form for articles.** Any single-argument builtin or script function may be written either way, and the two spellings compile to identical code.

A function taking more or fewer than one argument MUST use the parenthesised form. `[verb(actor, take)]` has two arguments and cannot be juxtaposed; this keeps the sugar unambiguous with one token of lookahead.

The rule exists because message text is the thing authors write most, and

```stardata
failureMsg = "You can't reach [the noun] from [the second]."
```

reads considerably better than the same line spelled with calls. Authors arriving from Inform will find it familiar.

`if`, `else` and `end` are reserved (§3.9) and are never read as applications, so `[if is_dark]` remains the conditional of §9.1.

#### 9.2.2 Capitalisation

A builtin name whose first character is upper case capitalises the first character of its result. This is implemented **once, generically**: if a name does not resolve and begins with an upper-case letter, the lower-case form is looked up and its output capitalised. No builtin needs a capitalised twin, and the rule applies to author-defined script functions too.

```stardata
successMsg = "[The actor] [verb(actor, take)] [the noun]."
# player -> "You take the brass key."
# NPC    -> "Vex takes the brass key."
```

#### 9.2.3 Standard builtins

Implementations SHOULD provide at least:

| Builtin | Meaning |
|---|---|
| `the` | definite article and name — "the brass key" |
| `a` | indefinite article and name, choosing *a* or *an* from the lexicon's `article` (§9.5.2), which is phonetic rather than orthographic — "an hour", "a unicorn" |
| `name` | the bare name, no article |
| `plural` | the plural form |
| `verb` | `verb(actor, ACTION)` — the form agreeing with the actor (§9.5.3) |
| `is`, `has` | irregular auxiliaries agreeing with their argument |
| `pronoun`, `possessive`, `reflexive` | |
| `number`, `ordinal` | |
| `time`, `date` | formatted against the calendar (§11.6) |
| `HolderOf`, `ContentsList` | |
| `tip` | opens an explicit tooltip span (§9.4) |

Capitalised forms (`The`, `A`, `Name`, `Number`, …) follow from §9.2.2 and are not separately listed.

```stardata
successMsg = "You are already holding [the noun]."
tooltip    = "Damage: [DamageString(self)] · Range: [self.range]"
```

### 9.3 Styling

Styling is **semantic, not literal**. An author names a style; a theme maps names to concrete attributes. This is what allows the same text to render correctly in a rich Qt window, a browser, and a monochrome Glk terminal.

- `@style(name)` as a **value annotation** (§5.4) sets the style of the whole string.
- `@style(name)` **within** a template opens a span that ends at the next `@style(...)`, at `@endstyle`, or at the end of the template, whichever comes first.

```stardata
tooltip = @style(tooltip_card) "[Name self]\n"
    "@style(stat_line)Damage: [DamageString(self)]\n"
    "@style(stat_line)Range: [self.range]\n"
    "@style(flavour)[self.short_description]"
```

Styles are declared with `style = { id = … }` and mapped by the active theme. An undeclared style name MUST be a compile error.

### 9.4 Tooltips

A template builtin declared as *object-naming* (`the`, `a`, `name`, and their capitalised forms) automatically wraps its output in a span carrying the object reference, which a capable frontend renders as a hover target (§12.3 of the proposal). No author action is required.

An explicit form exists for spans that are not a name:

```stardata
description = "A [tip(plasma_cutter)]battered yellow tool[end] lies on the bench."
```

Tooltip *content* is produced lazily by evaluating the referenced object's `tooltip` template, so a screen of item names costs nothing until one is hovered.

### 9.5 The lexicon and adaptive text

A message whose actor may be the player or an NPC needs "You take" and "Vex takes" from one template. The engine therefore needs word forms, and this section specifies where they come from.

**The core is language-neutral.** It knows that a word has forms, and that forms are selected by grammatical features. Which features exist, and how missing forms are inferred, come from a language pack.

#### 9.5.1 Enumerated forms

An `action` SHOULD declare its verb's forms. Actions are a closed set, so this is exact rather than inferred, and translating a game means translating a table rather than porting a morphology engine.

```stardata
action = {
    id   = take
    verb = { base = "take"  third = "takes"  past = "took"
             past_participle = "taken"  present_participle = "taking" }
}
```

#### 9.5.2 The lexicon

Open-class words — nouns, and verbs used in author-written messages — are inferred by the language pack's rules. A `lexicon` block overrides the inferences that are wrong:

```stardata
lexicon = {
    lang = en
    noun = { base = "knife"   plural = "knives" }
    noun = { base = "sheep"   plural = "sheep" }
    noun = { base = "hour"    article = "an" }    # phonetic, not orthographic
    verb = { base = "polish"  third = "polishes" }
}
```

An implementation MUST report every form it inferred rather than read from a `lexicon` or an `action`, so that inference is reviewable and cannot silently become load-bearing. This report is the mechanism that keeps the rule table a convenience rather than the specification.

#### 9.5.3 Adaptive substitutions

These template builtins (§9.2) resolve through the lexicon:

| Substitution | Produces |
|---|---|
| `[the X]`, `[a X]`, `[Name X]` | article and capitalisation, using the lexicon's `article` where present |
| `[verb(actor, ACTION)]` | the form agreeing with the actor's person and number |
| `[is(X)]`, `[has(X)]` | irregular auxiliaries, tabulated |
| `[plural(X)]`, `[number(n)]` | |
| `[pronoun(X)]`, `[possessive(X)]`, `[reflexive(X)]` | |

Narrative voice is set once per project and applies to every substitution:

```stardata
defaults = {
    narrative_person = second      # first | second | third
    narrative_tense  = present     # present | past
}
```

#### 9.5.4 Selection, not generation

Rules infer forms; they never *define* them. A form present in a `lexicon` or an `action` MUST take precedence over any inferred one, and a language pack MUST be able to supply forms for a language whose morphology it cannot generate. This is what makes localisation a matter of adding tables rather than adding engines.

### 9.6 Localisation

```stardata
loc = {
    lang = en
    already_holding = "You are already holding [the noun]."
    opened_default  = "You open [the noun]."
}
```

- A `LocKey` (`$already_holding`) resolves against the loaded locale, falling back to the project's declared source language, then to the key name itself rendered visibly as `«already_holding»` so that a missing string is obvious in play rather than blank.
- **Inline strings are implicitly assigned generated keys** at compile time. A game can therefore be localised after the fact without the author having restructured anything, and Starbase offers "extract to locale file" as a single command.
- Keys MUST be unique within a language. A duplicate MUST be an error citing both spans.

---

## 10. The condition language

A `condition_block` evaluates to a boolean. The same language is used by action `restrictions`, rule `conditions` and `when`, dialogue choice availability, quest `complete_when`, schedule entry `conditions`, and NPC goal guards. It is learned once.

### 10.1 Structure

Within a condition block, statements are combined with **implicit AND**, in order:

```stardata
restrictions = {
    carrying = { holder = actor  obj = noun }
    NOT = { noun = { of_class = fixed_in_place } }
    OR = {
        actor = { strength >= 14 }
        carrying = { holder = actor  obj = crowbar }
    }
}
```

Evaluation order follows source order, and evaluation **short-circuits**. This is observable, because a condition may call a script, so the order guarantee of §5.1 matters here.

### 10.2 Object-scoped conditions

A key naming an object slot (`actor`, `noun`, `second`, `self`, `speaker`) or an object id opens a block of conditions about that object:

```stardata
noun = {
    of_class = container
    open == yes
    weight <= 20
}
```

Inside such a block, a key naming a property of that object with a comparison operator tests it.

### 10.3 Combinators

| Key | Arity | Meaning |
|---|---|---|
| `NOT` | many | negation of the enclosed block |
| `OR` | many | at least one enclosed statement holds |
| `AND` | many | all hold (explicit form of the default) |
| `COUNT_AT_LEAST` | one | `{ n = 2  … }` — at least `n` of the enclosed statements hold |

### 10.4 Standard predicates

| Predicate | Form |
|---|---|
| `of_class` | `{ of_class = container }` |
| `has_trait` | `{ has_trait = openable }` |
| `is` | `{ is = brass_key }` — identity |
| `carrying` | `{ holder = … obj = … }` |
| `wearing` | `{ holder = … obj = … }` |
| `containing` | `{ holder = … obj = … relation = in }` |
| `held_by` | `{ held_by = { of_class = person } }` |
| `in_location` | `{ in_location = corridor }` |
| `in_sector` | `{ in_sector = station_alpha }` |
| `present` | `{ present = companion_kira }` — co-located with the viewpoint |
| `participating` | `{ participating = companion_kira }` — in the current dialogue, possibly remote |
| `visible` / `reachable` | `{ visible = noun }` |
| `visited` | `{ visited = command_bridge }` |
| `examined` | `{ examined = blood_trail }` |
| `flag_set` | `{ flag_set = captain_found }` — sugar for a `bool` global (§6.4.1) |
| `global` | `{ global = { alert_level == high } }` |
| `has_prop` | `{ has_prop = damage }` — runtime test **and** static narrowing (§8.8.3) |
| `compare` | computes a value and tests it — §10.6 |
| `contains` | `{ contains = { collection = seen_endings  value = good_end } }` |
| `is_empty` | `{ is_empty = seen_endings }` |
| `quest_state` | `{ quest = … state = unstarted\|active\|complete\|failed }` |
| `quest_flag` | `{ quest_flag = captain_confronted }` |
| `said_before` | `{ said_before = kira_i_station }` |
| `attitude` | `{ npc = … toward = … at_least = 40 }` |
| `faction_alert` | `{ faction_alert = high }` |
| `dead` | `{ dead = captain_reyes }` |
| `random_chance` | `{ random_chance = 25 }` — percent, from the world RNG |
| `any_actor` | `{ any_actor = { in_combat == yes } }` |
| `script` | `{ fn = … }` |

Libraries add predicates by declaring them; the list above is `stdlib/core` plus the entries `stdlib/starscape` contributes (`attitude`, `faction_alert`, `dead`, `any_actor`).

### 10.5 `failureMsg`

When a **restriction** fails, the action has definitely been attempted and the player is definitely owed an explanation. A restriction that fails without a message is not a neutral omission — it forces the engine onto a generic fallback ("You can't do that right now."), which is the single most common way a parser game feels unfinished.

Accordingly: in a restriction context, a condition block **SHOULD** carry a `failureMsg`, and an implementation SHOULD warn about any restriction with no reachable message (§15). The word is SHOULD rather than MUST only because §10.5.3's inheritance chain often supplies one from an enclosing scope.

```stardata
NOT = {
    carrying = { holder = actor  obj = noun }
    failureMsg = "You are already holding [the noun]."
}
```

`failureMsg` MUST NOT appear in a `conditions` or `when` block: those stages are silent by definition, and a message there would never be shown. This MUST be an error rather than a silent no-op.

A `compare` block (§10.6) is a predicate rather than a combinator, so it carries its own `failureMsg` and its readers carry none.

#### 10.5.1 Where the message goes

A `failureMsg` belongs on **the innermost block whose own failure is the reason the restriction failed**. Which block that is depends on the combinator, and getting it wrong produces a message that is never printed.

Note the example above: the message sits on the `NOT`, not on the `carrying` block inside it. That is not a stylistic choice. `NOT` fails when its contents *succeed*, so it is the `NOT` that has failed; the `carrying` condition succeeded, and a message attached to it would describe a state that is the opposite of what happened.

Generalising:

| Combinator | Fails when | Message goes on | Messages on children are |
|---|---|---|---|
| implicit AND (a plain block) | any child fails | the **failing child** | reachable |
| `AND` | any child fails | the **failing child** | reachable |
| `NOT` | its contents **succeed** | the `NOT` block itself | **unreachable** |
| `OR` | **every** child fails | the `OR` block itself | **unreachable** |
| `COUNT_AT_LEAST` | fewer than `n` children hold | the `COUNT_AT_LEAST` block | **unreachable** |

The underlying rule: `NOT`, `OR` and `COUNT_AT_LEAST` fail *as a whole*. No individual child's failure is the reason, so no child can explain it. `AND` and the implicit conjunction fail *because of a specific child*, so the child explains it.

Formally, a `failureMsg` is **reachable** if and only if the block carrying it can be reached from the root of the restriction block by descending through conjunction edges only — that is, through plain blocks and explicit `AND` blocks. The `NOT` / `OR` / `COUNT_AT_LEAST` node at such a position is itself reachable and carries its own message; everything below it is not. An unreachable `failureMsg` MUST be an error, since it is invariably a mistake and always a silent one.

```stardata
restrictions = {
    # Reachable: the failing child of the implicit AND.
    noun = { has_trait = portable
             failureMsg = "[the noun] [Is(noun)] fixed in place." }

    # Reachable: on the NOT, which is what fails.
    NOT = { carrying = { holder = actor  obj = noun }
            failureMsg = "You are already holding [the noun]." }

    # Reachable: on the OR. Neither branch can explain the failure alone —
    # the player failed to be strong enough AND failed to have a crowbar.
    OR = {
        actor    = { strength >= 14 }
        carrying = { holder = actor  obj = crowbar }
        failureMsg = "It won't shift. Not with your bare hands, anyway."
    }
}
```

#### 10.5.2 Which message the player sees

Restriction evaluation short-circuits in source order (§10.1). The **first** failing condition supplies the message; later conditions are not evaluated and their messages never appear.

This is the author's control over which explanation is given, and it argues for a deliberate ordering: put the most specific and most informative restriction first, and the broad catch-all last. An action whose first restriction is a generic reachability check will always say the generic thing, even when a much better explanation was available two lines down.

#### 10.5.3 Fallback chain

When a restriction fails, the message is resolved in this order, taking the first that exists:

1. The `failureMsg` on the failing block itself.
2. The `failureMsg` on the nearest enclosing reachable ancestor, walking up conjunction edges to the root of the restriction block.
3. The `failureMsg` declared on the enclosing `rule`, if the failing restriction came from one.
4. The `failureMsg` declared on the `action`.
5. The stdlib's generic fallback, `$action_blocked_default`.

Steps 2–4 exist so that an author can write one sensible message for a whole action and only override it where a specific explanation is worth the words. Step 5 is a localisation key, not a hard-coded string, so a game may replace the generic wording project-wide with a single `loc` entry — but reaching step 5 in play SHOULD be treated as a content bug.

---

### 10.6 `compare` — testing a computed value

Everything else in the condition language tests something that is already a value: a property, a global, a relationship. But `how many waypoints are left` and `what is at index 0` **compute** a value, and the grammar has nowhere to put the result. A statement is `Key Op Value` (§4), so `count_of = { … } >= 2` is not merely unusual, it is ungrammatical: it has two operators and a dangling comparison.

`compare` is the one place a computed value is produced and tested:

```stardata
compare = {
    count_of = seen_endings        # exactly one reader, producing a value
    value >= 2                     # one or more tests against it, implicitly ANDed
    value <= 5
}
```

- **Exactly one reader.** Zero or two or more is an error.
- **One or more `value` statements**, combined with implicit AND, so a range test needs no extra machinery.
- `value` is a keyword within `compare` and names the reader's result. It has no meaning elsewhere.
- The reader's result type and the operand's type must be comparable; the compiler checks this, since both are statically known.

#### 10.6.1 Readers

| Reader | Produces |
|---|---|
| `count_of = <collection>` | element count, as `int` |
| `at = { collection = C  index = N }` | the element at `N`; the collection's element type |
| `map_get = { collection = C  key = K }` | the value stored under `K` |
| `prop_of = { obj = O  prop = P }` | a property of a named object — the only way to read a property of something that is not a slot |
| `prop_or = { obj = O  prop = P  default = D }` | as above, yielding `D` when `P` is absent (§8.8.3) |
| `global_value = <id>` | a global's current value |
| `script_value = { fn = F }` | a script's return value, for anything the declarative readers cannot express |

Note that `script_value` is distinct from the `script` predicate (§10.4): `script` returns a boolean and is a condition in its own right; `script_value` returns a value to be compared.

#### 10.6.2 Operands

The right-hand side of a `value` statement may be a literal scalar, a `const` id, or a `global` id. It may **not** be a second reader — comparing two computed values is a job for `script_value`, and allowing it here would mean nesting readers inside operands for a case that rarely arises.

```stardata
restrictions = {
    compare = {
        prop_or = { obj = noun  prop = damage  default = 0 }
        value > 3
        failureMsg = $too_feeble
    }
}

conditions = {
    compare = {
        at = { collection = waypoints  index = 0 }
        value == docking_gantry
    }
}

conditions = {
    compare = {
        global_value = core_temp
        value >= max_reactor_temp        # a const
    }
}
```

#### 10.6.3 Why not a general expression language

`compare` is deliberately shallow: one reader, no arithmetic, no nesting. A full expression grammar inside conditions would be more powerful and would also make conditions unrenderable as an editor form and unanalysable by the compiler — the same trade §1.4.3 makes for effect blocks. Anything needing real computation uses `script_value`, where the boundary is visible in the source.

---

## 11. The effect language

An `effect_block` mutates the world. Effects execute **in declaration order** (§5.1); this is load-bearing, since `try_action` and `script` effects observe state left by earlier ones.

An effect block contains no conditionals and no loops. This restriction is deliberate: it is what makes an effect block renderable as an editor form and analysable by the compiler. Anything needing control flow uses `script` (§12).

### 11.1 Standard effects

| Effect | Form |
|---|---|
| `move` | `{ obj = … to = … relation = carried }` |
| `set` | `{ target = … prop = … value = … }` |
| `add` | `{ target = … prop = … amount = … }` — numeric increment |
| `remove_from_play` | `{ obj = … }` |
| `restore_to_play` | `{ obj = … to = … relation = in }` |
| `trigger` | `{ event = … in_sector = … }` — emits an event for interrupt rules |
| `set_flag` / `clear_flag` | `{ set_flag = captain_found }` — the global MUST be declared `bool` (§6.4.1) |
| `set_global` | `{ id = alert_level  value = high }` |
| `add_global` | `{ id = times_caught  amount = 1 }` — numeric |
| `list_add` / `list_remove` / `list_clear` | `{ collection = …  value = … }` or `index = …` (§6.5) |
| `map_put` / `map_remove` | `{ collection = …  key = …  value = … }` |
| `start_quest` | `{ quest = … }` |
| `advance_quest` | `{ quest = … stage = … }` |
| `fail_quest` | `{ quest = … }` |
| `pin_sector` / `unpin_sector` | `{ sector = … reason = … }` (§11.4) |
| `try_action` | §11.2 |
| `enter_dialogue` | §11.3 |
| `adjust_attitude` | `{ npc = … toward = … amount = -20 }` |
| `script` | `{ fn = … }` |

### 11.2 `try_action`

Redirects to another action — the mechanism by which `TAKE APPLE` becomes `REMOVE APPLE FROM BOX`.

```stardata
try_action = {
    action = remove_from
    noun   = noun
    second = HolderOf(noun)
    on_failure = abort           # abort | continue | succeed
    report = yes                 # yes | no | only_on_failure
    inherit_duration = no
}
```

Normative semantics:

1. The inner action runs its **complete** pipeline — its own before, conditions, restrictions, effects, report, and every rule that applies to it. It is not a shortcut to the inner action's effects.
2. **One round total.** The turn consumption and duration are those of the outermost action. The inner action's `duration` and `advances_turn` are ignored unless `inherit_duration = yes`.
3. `on_failure` determines the outer action's fate: `abort` (default — outer fails, and the inner action's failure message is what the player sees), `continue` (outer proceeds to its remaining effects), `succeed` (the failure is swallowed).
4. The `actor` is inherited unless explicitly overridden.
5. Recursion depth is bounded (default 16). Exceeding it MUST raise a catchable authoring error naming the cycle. The compiler MUST additionally reject statically provable cycles.

### 11.3 `enter_dialogue`

```stardata
enter_dialogue = {
    dialogue = reactor_console_interface
    node     = main_menu            # optional; defaults to the dialogue's entry rules
    on_exit  = resume               # resume | end_round
}
```

The enclosing effect block **suspends** at this point and resumes at the following effect when the dialogue ends. `on_exit = end_round` instead ends the acting actor's slot.

A dialogue entered this way is an ordinary dialogue in every respect. Its speaker MAY be a non-animate object, or `none` for a narrator voice.

### 11.4 Sector pinning

`pin_sector` and `unpin_sector` are **reference-counted by `reason`**. Two systems pinning the same sector do not cancel each other, and outstanding pins are recorded in the save. Party members hold automatic pins with `reason = party_member_present` (§11.7).

### 11.5 Object references in effects

An effect's object-valued fields accept: a slot name (`actor`, `noun`, `second`, `self`), an object id, or a template-language call returning an object (`HolderOf(noun)`).

### 11.6 Time

The world clock is a signed 64-bit count of **ticks** since the calendar epoch. One tick is one second unless a calendar declares otherwise.

There is exactly one clock. Locations do not experience differing elapsed durations. A sector MAY declare a `local_clock` — a calendar and an offset — which affects only the *display* of time and the compilation of `clock_time` values written in that sector's terms.

Actions declare a `duration` in ticks; `default` takes the project's default (60 in `stdlib/core`). Actors carry a `busy_until` tick and are skipped in the actor loop until the clock reaches it.

### 11.7 Party

```stardata
party = {
    id = player_party
    viewpoint = pc
    member = { actor = pc  role = leader }
    member = {
        actor = companion_kira
        role = companion
        control = { in_combat = player  out_of_combat = ai }
        follow  = { target = viewpoint  distance = same_room }
    }
    max_size = 4
}
```

- `viewpoint` determines whose senses drive room descriptions and scope. It is **separate from control**, so player-controlled companions do not require a switchable viewpoint.
- Each member holds an automatic pin (§11.4) on their own sector, so a separated companion remains fully simulated.
- `follow` compiles to a standing low-priority goal, so it is naturally pre-empted by combat and resumes afterwards.

---

## 12. Scripting interface

A value of type `script` names a Starscript (Lua 5.4) function. Nothing about the Lua dialect is specified here beyond the boundary.

### 12.1 Scripted rules and actions

A `rule` or `action` MAY replace `conditions`, `restrictions`, `effects` and `successMsg` entirely with a single `script`:

```stardata
rule = {
    of_action = take
    when   = { noun = { has_trait = cursed } }
    script = handle_cursed_take
}
```

`when` MUST remain declarative, because the compiler's dispatch index is built from it. A rule with no `when` falls into the scanned dynamic list, and the compiler MUST report the size of that list.

The function returns one of three verdicts:

| Verdict | Meaning |
|---|---|
| `star.ok()` | the action succeeds; the function has applied its own effects and emitted its own text |
| `star.fail(msg)` | the action fails with this message |
| `star.pass()` | this rule declines; processing continues to the next rule as though it had not matched |

`star.pass()` has no declarative equivalent and is what makes scripted rules composable.

Phase annotations still apply: `script = @before handle_cursed_take`.

### 12.2 The capability rule

> Stardata is a curated subset of what Starscript can express, chosen for editability and static analysis. Everything expressible in Stardata is expressible in Starscript. **The reverse is deliberately not true** — Stardata has no conditionals within effect blocks, no loops, and no arithmetic beyond accumulate-and-compare.
>
> **Corollary:** no *engine capability* is reachable only from Stardata. Every data primitive has a scripting equivalent, so an author who outgrows the declarative form never loses anything by moving to a script.

---

## 13. Projects and load order

### 13.1 The project manifest

A project has one `project.star` at its root:

```stardata
project = {
    id = derelict
    title = "Derelict"
    author = "Adrian Welcker"
    version = "0.1.0"
    ifid = "8F4B2C1A-..."
    source_language = en

    uses = { star_core starscape }

    player        = pc
    start_room    = your_cell
    entry_sector  = station_alpha

    defaults = {
        action_duration = 60
        advances_turn   = on_success
    }

    simulation = {
        offstage_default        = catch_up
        simulate_max_rounds     = 5000
        simulate_time_budget_ms = 20000
        simulate_progress       = yes
        default_combat_response = flee
    }
}
```

### 13.2 Load order

Sources are loaded in this order, and later declarations win where §5.4's combination rules give a winner:

1. `stdlib/core`
2. libraries named in `uses`, in declaration order
3. the project's own sources, in a deterministic traversal: `project.star` first, then remaining files sorted by path
4. mods, in the player's configured order (runtime only)

Within a single file, source order governs.

An implementation MUST NOT make load order depend on filesystem enumeration order, since that differs between platforms and would make builds irreproducible.

### 13.3 Libraries

```stardata
library = {
    id = starscape
    version = "1.0.0"
    display_name = $lib_starscape_name
    requires = { star_core >= "1.0.0" }
    uses_editor_feature = { rpg quests dialogue }
    provides_schema = { stat_block combat_style loot_table }
}
```

`uses_editor_feature` toggles editor panels. The *content* of those panels comes from the library's schema declarations (§7.1), not from editor code — so a ruleset with an `insight` stat and no `strength` gets a correct editor with no editor changes.

Version constraints use `>=`, `<=`, `==` and `~>` (compatible-within-minor) against semantic versions.

---

## 14. Implementation requirements

### 14.1 Determinism

1. Every engine API returning a collection MUST return it in a documented deterministic order.
2. All randomness MUST derive from a single seeded generator serialised with the world state. No other source of randomness is available to authors.
3. `decimal` arithmetic MUST be fixed-point and therefore bit-identical across platforms, including WebAssembly.

### 14.2 Lossless round-trip

A Level 1 implementation MUST parse into a **lossless concrete syntax tree** in which comments, whitespace, ordering and original formatting are preserved as trivia attached to nodes, such that re-printing an unmodified tree reproduces the input **byte for byte**, including the BOM and line-ending style.

An edit to one node MUST re-print only the affected span. Formatting and comments outside that span MUST be preserved exactly.

This is the property that allows a graphical editor and a hand-edited, version-controlled text format to coexist. Implementations SHOULD test it with property-based random-edit fuzzing as a continuous-integration gate.

### 14.3 Diagnostics

Every diagnostic MUST carry a source span (file, byte offset, line, column) and SHOULD carry a fix-it suggestion where one is well defined. The specific diagnostics required by this document are:

| Condition | Severity |
|---|---|
| Mixed list and record contents in one block (§5.2) | error |
| Duplicate key where `arity = one` (§5.3), citing both spans | error |
| Unknown key in a closed schema (§7.3), with suggestion | error |
| Unknown annotation (§3.7) | error |
| `==` in a binding context (§3.6) | error |
| Bare `=` in a condition context (§3.6) | warning |
| `true` / `false` used as a value (§3.8) | error |
| Type mismatch against the declared type (§6.2) | error |
| Unresolvable `ref<C>`, or a reference to the wrong class | error |
| Trait property conflict without `resolve` (§8.3) | error |
| Undeclared global named by `set_flag` / `clear_flag` / `flag_set`, or one not of type `bool` (§6.4.1) | error |
| Reference to an undeclared `global` or `const` (§6.4) | error |
| Property read that is definitely absent for the slot's static type (§8.8.2) | error |
| Property read that is possibly absent and not narrowed (§8.8.3), with a `has_prop` fix-it | error |
| Local `prop_def` redeclaring an inherited name with a different type (§8.7) | error |
| Local `prop_def` redeclaring an inherited name with the same type (§8.7) | warning |
| An object whose names are a strict subset of another's reachable in the same place (proposal §6.4.1) | warning |
| A declared `global` or `const` never read (§6.4) | warning |
| Containment cycle (§8.5) | error |
| `failureMsg` in a silent context — a `conditions` or `when` block (§10.5) | error |
| `failureMsg` in an unreachable position, below a `NOT`, `OR` or `COUNT_AT_LEAST` (§10.5.1) | error |
| A restriction with no reachable `failureMsg` anywhere in its fallback chain (§10.5.3) | warning |
| Statically provable `try_action` cycle (§11.2) | error |
| Undeclared style name (§9.3) | error |
| Duplicate localisation key (§9.6) | error |
| `-=` removing an absent entry (§6.3) | warning |
| Two `class_extension`s setting the same default (§8.2) | info |
| Project crossing 64 declared traits (§8.3) | info |

---

## 15. Reserved for future use

The following are reserved and MUST currently be rejected, so that adding them later is not a breaking change:

- The characters `[` and `]` outside string literals. They are reserved exclusively for the template language and parser grammar tokens, and MUST NOT acquire a block meaning.
- The operators `*=`, `/=`, `=>`, `->`, `::`.
- The keys `import`, `include`, `macro`, `template` at top level.
- Annotations `@deprecated`, `@since`, `@experimental`.
- The `voice` key on dialogue nodes (§11.3 of the proposal): reserved as `resource`-typed so that voice acting can be added without a data-model change.

---

## Appendix A — Decisions made by this specification

The proposal left the following under-determined. This specification settles them, and each is a candidate for review rather than a settled matter.

| # | Decision | Rationale |
|---|---|---|
| A1 | String literals MUST NOT span lines; adjacent literals concatenate (§3.5.1) | Makes an unterminated quote a one-line error rather than swallowing the rest of the file |
| A2 | `Decimal` rejects more than three fractional digits rather than rounding (§3.4) | Silent rounding in a damage formula is exactly the failure fixed-point exists to prevent |
| A3 | `true`/`false` reserved but invalid (§3.8) | Purely to give arrivals from other formats a good diagnostic |
| A4 | Annotations may take arguments; `@priority(3)`, `@platform(glk, cli)` (§3.7) | The proposal used both parenthesised and bare forms; unified |
| A5 | `combine` modes named and `smart` introduced (§5.4.2) | The proposal specified the rule-block defaults behaviourally; this names the mechanism so libraries can use it |
| A6 | `none` as a distinct value from `inherit` (§5.5) | "Explicitly empty" and "unchanged" are different, and conflating them makes clearing a reference impossible |
| A7 | `string` type distinct from `text` (§6.2) | Machine-facing values should not be localisable or interpolatable |
| A8 | `@style(name)` spans to the next style directive or end (§9.3) | Matches the proposal's example; the alternative (explicit closing) is noisier |
| A9 | Missing localisation renders as `«key»` rather than blank (§9.6) | A missing string must be visible in play |
| A10 | Condition evaluation short-circuits in source order (§10.1) | Observable because conditions may call scripts, so it must be specified rather than left to the implementation |
| A11 | `failureMsg` in a silent context is an error (§10.5) | It would otherwise be a message the author writes and never sees |
| A12 | Load order sorts by path rather than filesystem order (§13.2) | Reproducible builds across platforms |
| A13 | `[` and `]` reserved against ever becoming block syntax (§15) | Guards the v0.2 decision to drop the ordered/unordered distinction |
| A14 | Juxtaposition as single-argument application (§9.2.1) | Message text is what authors write most; `[the noun]` reads better than `[the(noun)]`. Stated as one general rule so there is no special-cased article syntax |
| A15 | Capitalisation by initial letter, resolved generically (§9.2.2) | Avoids a capitalised twin for every builtin, and extends to author-defined functions for free |
| A17 | Flags are sugar over declared `bool` globals, not a separate store (§6.4.1) | As undeclared strings they are a silent-typo generator, which is exactly what the schema layer exists to prevent |
| A18 | Object-local `prop_def` still requires a declaration (§8.7) | One line buys typo detection, a type, an editor widget and a stable save key; the alternative reintroduces untyped looseness |
| A19 | Property access is statically checked with narrowing, plus an explicit runtime escape (§8.8.3) | Runtime-only moves authoring errors into play; static-only cannot reach scripts or honest subclass-varying cases |
| A20 | An absent property raises rather than defaulting (§8.8.4) | A `0` that should have been an error yields a game that is subtly wrong, which is far harder to find than one that is obviously broken |
| A22 | `compare` as the single home for computed values (§10.6) | `count_of = { … } >= 2` is not merely unusual but ungrammatical — a statement is `Key Op Value`, so a dangling second operator has nowhere to live. One shallow form beats scattering value-producing predicates that cannot express their own result |
| A21 | Replication is specified but unscheduled; its parser constraint is not (§8.9) | Indistinguishable candidates arise from hand-declared objects too, so the parser needs the rule regardless |
| A16 | Naming builtins `the` / `a` / `name` rather than `theName` / `aName` / `PrintName` | The original notes used the longer forms; they read poorly under juxtaposition, and nothing has shipped, so the rename is free |

## Appendix B — Change log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 2026-08-11 | Initial specification, derived from `docs/proposal.md` v0.3 |

## Appendix C — Standard top-level forms

Forms supplied by `stdlib/core` unless noted. Libraries add more by declaring schemas (§7.2); this list is not closed.

| Form | Purpose | Specified in |
|---|---|---|
| `project` | Project manifest; exactly one per project | §13.1 |
| `library` | Library metadata, dependencies, editor features | §13.3 |
| `schema` | Declares a form or nested block shape | §7.2 |
| `enum` | Declares an enumerated value set | §6.2 |
| `style` | Declares a semantic text style | §9.3 |
| `global` | A mutable, saved, typed variable not owned by any object | §6.4 |
| `const` | An immutable, unsaved named value | §6.4 |
| `calendar` | Units and epoch for displaying and compiling times | §11.6 |
| `loc` | Localisation table for one language | §9.6 |
| `class` | Declares a class | §8.1 |
| `class_extension` | Adds properties or changes defaults on an existing class | §8.2 |
| `backdrop` | An object present in several rooms at once | §8.6 |
| `door` | A two-sided object joining two rooms | §8.6.1 |
| `trait` | Declares an orthogonal capability bundle | §8.3 |
| *class name* | Instantiates an object of that class | §7.4 |
| `sector` | Declares a loading, simulation and pinning unit | §11.4 |
| `action` | Declares a player- or NPC-performable action | §10, §11 |
| `rule` | Modifies an action, or reacts to an event | below |
| `turn_hook` | Registers a ruleset hook at a named turn phase | below |
| `quest` | Declares a quest, its stages and completion predicates | §10, §11 |
| `dialogue` | Declares a conversation graph | below |
| `goal_def` | Declares a multi-round NPC plan | below |
| `schedule` | Declares an NPC's time-of-day routine | §11.6 |
| `bark_table` | Weighted one-line NPC utterances | below |
| `lexicon` | Word forms overriding the language pack's inferences | §9.5.2 |
| `party` | Declares a party, its members and control model | §11.7 |

### C.1 `rule`

A rule keys on **either** an action or an event, never both:

- `of_action = <action>` — modifies that action's pipeline. Combined with `when` (a condition block) it feeds the compiler's dispatch index.
- `of_event = <event>` — an interrupt, run when the named event is emitted by a `trigger` effect (§11.1) or by the engine. Within an event rule, `self` is bound to the reacting object.

A rule with neither MUST be an error. A rule with no `when` is legal but falls into the scanned dynamic list, and the compiler MUST report that list's size.

### C.2 `turn_hook`

```stardata
turn_hook = { id = … phase = … priority = 100 conditions = { … } script = … }
```

`phase` names one of the turn sequence's registration points: `round_start`, `upkeep`, `initiative`, `reaction`, `round_end`. Higher `priority` runs first within a phase; ties break by load order.

### C.3 `dialogue`

Contains `participant` (many), `entry` (many, tried in order), and `node` (many). A node contains `text`, optional `speaker`, `on_enter`, `once`, `goto`, and any number of `choice` and `interjection` entries.

- `speaker` MAY name any object, not only a `person`, and MAY be `none` for a narrator voice.
- `goto = END` leaves the dialogue; `goto = { dialogue = … node = … }` jumps across dialogues.
- An `interjection` with `joins = yes` (the default) promotes its speaker to an active participant for the remainder of the conversation. This is the structural difference from a bark, which changes nothing.

`enter_dialogue` (§11.3) accepts `dialogue = auto`, which selects the dialogue declared on the acted-upon object — the ordinary case for `talk to [someone]`.

### C.4 `goal_def`

Contains `params` (a list block of names), any number of ordered `step` entries, an optional `abandon_when` condition block, and `repeat = yes` for standing goals such as following.

### C.5 `bark_table`

Contains `of_npc` and any number of `line` entries, each with `text`, `weight`, optional `conditions` and `once`. Barks and dialogue interjections share their selection machinery — weighting, condition filtering, once-tracking, last-said suppression — but remain distinct constructs, because only an interjection alters conversational state.
