# Stardata Format Specification

**Version:** 0.2 (draft) · **Date:** 2026-08-17 · **Status:** Normative
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

### 1.2.1 What this document specifies, and what merely uses it

This document describes two things, and a reader should know which they are looking at. The distinction is **mechanism versus vocabulary** (proposal §2.1.1):

| | **Stardata, the format** | **The STAR core vocabulary** |
|---|---|---|
| Levels | 1 and 2 | 3 |
| Implemented by | `libs/stardata` | `libs/starcore` |
| Content | lexical structure, grammar, block semantics, types, the schema *mechanism*, the declaration layer, load order, round-trip | the object model, containment, templates, and the condition and effect vocabularies |

Everything in the left column would be equally true of a format used for a spreadsheet or a spacecraft telemetry log; everything in the right column exists because this is an interactive fiction system.

A Level 2 implementation can validate any Stardata file against any schema, declare classes and traits, instantiate objects and type-check every property — and knows nothing about rooms. A Level 3 implementation knows what `holder` means, that a `restrictions` block owes the player a message, and in what order an action's stages run.

**The line does not follow the section numbering, and this table used to claim it did.** Two questions decide where something belongs, and they have different answers for the same section:

- **Who parses the declaration?** A *format form* (§7.2.4) is one `libs/stardata` reads with a hard-coded reader of its own. `class`, `trait`, `class_extension`, `enum`, `global` and `const` are all format forms, so §8.1–§8.3 and §6.4 are the format's even though they sit among the vocabulary sections.
- **Who reads the data?** The engine. `libs/starcore` decides what a class *means* — that an object has a location, that a global appears in a save delta — without parsing any of it.

So the crossings are these, and naming them is more useful than a table that implies there are none:

| Section | Parsed by | Read by |
|---|---|---|
| §6.4 `global`, `const` | `libs/stardata` (format forms) | the engine: save state, deltas, undo |
| §8.1–§8.3 `class`, `trait`, `class_extension` | `libs/stardata` (format forms) | the engine: the object model |
| §8.4–§8.8 property resolution, narrowing | `libs/stardata` | both |
| §8.5 placement sugar, §9–§12 | `libs/stardata` validates against a schema | `libs/starcore` |
| §7.2.5.1 `core_requirement` | `libs/stardata` (a format form) | `libs/stardata`, before core sees anything |

The last row is the one that shows *reserved to core* and *declared by core* are separate axes: only core may write a `core_requirement` (§7.2.5.1), and the form is the format layer's, because the format layer is what refuses everybody else.

The two halves live in one document because splitting a specification whose cross-references are this dense costs more than it currently returns. They will separate when `docs/runtime-spec.md` is written, since that document needs the right-hand column anyway.

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
Op ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-='
```

| Operator | Name | Valid in |
|---|---|---|
| `=` | bind | any context |
| `==` | equals | condition context only |
| `!=` | not equals | condition context only |
| `<` `>` `<=` `>=` | comparison | condition context only |
| `+=` | extend | binding context, collection-typed keys only |
| `-=` | reduce | binding context, collection-typed keys only |

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

Op        ::= '=' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+=' | '-='

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

Arity counts **binding** occurrences only — those using `=`. The modifier operators `+=` and `-=` do not bind; they transform whatever value is in effect, and any number of them may follow a binding, or stand alone and transform an inherited value. They apply in source order:

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
| `@platform(id, …)` | any | Present only on the listed frontends (`qt`, `web`, `glk`, `cli`, `mobile`); selected at run time, not compiled away |
| `@style(id)` | string | The string's default text style (§9.3) |
| `@replaces(lib)` | a whole top-level declaration | This declaration supersedes the one of the same id from library `lib` (§7.6) |

`@debug` and `@platform` are **conditional presence** annotations: neither modifies combination, and each determines whether the statement is there at all. They resolve at different times, and the difference is not a detail.

**`@debug` resolves at compile time.** The compiler strips it from a release build, and a statement it strips MUST behave exactly as though it had not been written, including for arity checking. It follows that a `@debug` statement does *not* stand apart from an unannotated one: a development build contains both, so two bindings of one `arity = one` key — one of them `@debug` — are a duplicate and MUST be reported as one.

**`@platform` resolves at run time.** A compiled game is frontend-agnostic: one artefact runs under every frontend, and which frontend is present is not known until it declares itself at session start. A compiler therefore MUST retain every `@platform` alternative rather than selecting among them, and the engine MUST select when the frontend is known. Stripping here would mean one build per frontend, which the distribution model does not have.

Arity follows from that. A set of statements binding one key, each carrying a `@platform` whose frontends are **pairwise disjoint**, is a single binding with a run-time selector, and is legal for `arity = one`:

```stardata
successMsg = @platform(qt, web, mobile) "The lock yields with a soft chime."
successMsg = @platform(glk, cli)        "The lock yields."
```

Two alternatives whose frontends **overlap** MUST be reported as a duplicate key (§5.3), citing both spans: under the frontend they share, the engine would hold two candidates and no rule to choose between them. A statement carrying no `@platform` is present on every frontend, and so overlaps every gated one — an unannotated binding and a `@platform`-gated binding of the same key are a duplicate, not a default and an exception. There is no fallback precedence; an author wanting one writes the general case as its own alternative over the remaining frontends, so that what runs where is legible at the point of writing rather than inferred from a rule.

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
| `ref<C>` | `Identifier`, `none` | a reference to an object of class `C` or a subclass; validated at compile time. `C` may also name a *form*, in which case the reference resolves to an instance of it, in the namespace that form's `unique_in` declares — `ref<action>` and `ref<sector>` are both written by the core-owned set |
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

#### 6.3.1 On the removal of `?=`

Earlier drafts specified a `?=` operator — "bind only if this key is currently unset, at any inheritance level" — intended to let a library offer a default a game could pre-empt regardless of load order. **It is removed.** The reasoning is recorded here because the operator appeared in published drafts and someone will ask.

**It could not be reasoned about locally.** Whether `x ?= 1` binds depends on every other declaration of `x`, at any inheritance level, in any file. Worse, the static question "is this a binding occurrence?" and the dynamic one "did this bind?" have different answers, and both are reasonable readings — which is not a hypothetical complaint: the first implementation and the specification disagreed about arity counting on the operator's very first contact with code.

**Its stated motivation was already satisfied.** Load order (§13.2) is stdlib, then libraries, then the project. A library's plain `=` is *always* pre-emptable by the project, because the project loads later. The case the operator existed for did not need it.

**Compilation collapses it anyway.** Starforge bakes resolved defaults into the object table (proposal §14.2), so `?=` carries no information past the compile boundary. A patch or mod layered above a `.spak` (proposal §14.1) sees an already-bound value, making `?=` in a patch close to inert — a half-guarantee that would mislead rather than help.

**And it ran against the grain of everything else.** Declared flags, `@replaces` naming its source and failing when that source declared nothing, `core_requirement`, sealing — every recent decision has moved toward explicit, named and loud. `?=` was a silent conditional whose effect you could not see at the site that wrote it.

The residual case — two *libraries* disagreeing, wanting an order-independent outcome — is real but rare, and the project already controls library order through `uses`. If it ever becomes pressing, the answer should be an explicit priority, in the manner of `@priority(n)` (§5.4.1), rather than a conditional binding.

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
- `global` and `const` are **format forms** (§7.2.4): the format layer parses and type-checks them, and the engine reads what they hold. That split is the whole of it — nothing about parsing `id`, `type` and `initial` is interactive fiction, and everything about a global appearing in a save delta is.
- The `initial` value of a `global`, and the `value` of a `const`, MUST be checked against the `type` declared beside it. That type is another key's value, so both keys are declared with §7.2's dependent type — `type_of = type` — and the ordinary value check does the rest.
- A declared `global` or `const` that nothing **reads** SHOULD be a warning. A write is not a read: a flag that is set and never tested is world state nothing depends on, and is usually a condition that was renamed or one that was never written.

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

**Conditions** (§10.4). Only the two that are genuinely boolean live here; anything that *computes a value* goes through `compare` (§10.6). The `<datum>` in each is a data reference as defined in §6.6:

| Predicate | Form |
|---|---|
| `includes` | `{ includes = { collection = <datum>  value = good_end } }` or `{ includes = { collection = <datum>  key = north } }` |
| `is_empty` | `{ is_empty = <datum> }` |
| `count_of` | via `compare` (§10.6) — for a map this counts entries |

`includes` is deliberately not called `contains`: §10.4 already has `containing`, which is about *physical* containment, and two predicates a letter apart meaning entirely different things is a trap.

#### 6.5.1 `includes` takes a value or a key

`includes` is the format's first form with two alternative arguments, and it is worth being explicit about why that is acceptable here.

```stardata
includes = { collection = seen_endings     value = good_end }   # list / set member
includes = { collection = npc_moods        value = hostile }    # a map's values
includes = { collection = location.exits   key   = north }      # a map's keys
```

- `value` applies to any collection. On a `map`, it searches the **values**.
- `key` applies **only** to a `map`. Using it on a `list` or `set` is an error naming the collection's declared type.
- Specifying **both** is an error. The author almost certainly means "is entry *K* equal to *V*", which is a different question with a much better spelling — so the diagnostic suggests the path form `location.exits.north == corridor` (§6.6.1), or `map_get` inside `compare` (§10.6) when the key is computed.

**Why unify rather than keep `has_key`.** The two really are one question asked over two domains, and the argument name says which — `key` and `value` are exactly the right words, so the call site is self-documenting without a second predicate name to learn. The overload is resolved statically by which argument is present, and enforced by the `exclusive_group` mechanism of §7.2.1 — which is not new machinery invented for this, since `rule`, `list_remove` and `present_in` all already require it and previously had no way to say so.

**One asymmetry worth knowing.** `key` is a hash lookup; `value` on a map is a linear scan. Sharing a name hides that, so it is documented here rather than in a footnote: if a map is large and searched by value often, the data probably wants a second map keyed the other way.

For maps, `count_of` and `is_empty` operate on **entries**, not on keys or values separately.

**Effects** (§11.1):

| Effect | Meaning |
|---|---|
| `list_add` | append, or insert at `index` |
| `list_remove` | remove by value, or by `index` |
| `list_clear` | empty the collection |
| `map_put` · `map_remove` | by key |

```stardata
effects = {
    # a bare name is a global; a dotted path is an object's property (§6.6)
    list_add    = { collection = seen_endings         value = good_end }
    map_put     = { collection = npc_moods            key = quartermaster_vex
                    value = hostile }
    list_remove = { collection = navcomp.waypoints    index = 0 }
}
```

Three constraints follow from decisions already made:

1. **Iteration order is defined** (§14.1). `list` preserves insertion order; `set` and `map` preserve declaration-then-insertion order. There is no unordered collection.
2. **There is no iteration in effect blocks** (§1.4.3). Walking a collection is a script operation. The declarative vocabulary covers add, remove, clear and test, which is what most authoring needs.
3. **Mutated collections are save state.** A collection whose contents differ from the compiled initial value is stored as a property override (proposal §5.2), which is the same mechanism as any other changed property.

An out-of-range `index` is a runtime error, not a silent no-op, for the reasons in §8.8.4.

### 6.6 Referring to data

A collection can be a global or an object's property, and `collection = waypoints` does not say which. Left unstated, the natural guess is that a bare name resolves against whatever object scope encloses it — so the same identifier would mean a property in one place and a global three lines later, and moving a condition into or out of an object block would silently change its meaning. That is not a hazard worth having.

**A `<datum>` is a reference to a value. Two forms, distinguished syntactically:**

| Form | Refers to | Example |
|---|---|---|
| bare identifier | a `global` or `const` (§6.4) | `seen_endings` |
| dotted path | a property of an object | `noun.waypoints`, `airlock_hatch.open` |

A path's first segment is a slot (`actor`, `noun`, `second`, `self`, `speaker`, `player`, `location`) or an object id. Intermediate segments MUST be `ref<C>`-typed, so `noun.holder.name` is legal and `noun.weight.name` is not.

`location` denotes the acting actor's current room. It was missing from earlier drafts, which is awkward given that "what is true of the room I am in" is among the most common things an author tests.

Dots inside identifiers are already lexical (§3.3), so a path is one token and no grammar change is needed. It is also the same notation templates use — `[noun.damage]`, `[self.range]` (§9.2) — so authors write one form of property reference throughout.

#### 6.6.1 Map keys are path segments

A segment following a `map<K,V>`-typed segment is a **key**, not a property name:

```stardata
restrictions = { location = { exits.north == corridor } }
```

`exits` is `map<direction, ref<room>>`, so `north` is read as a key of type `direction` and the path yields a `ref<room>`. Chaining continues normally: `location.exits.north.name` is the name of the room to the north.

This is unambiguous to the compiler, because whether a segment is a property or a key is decided by the *declared type* of the segment before it. It is also unambiguous to a reader, provided they know `exits` is a map — which the schema, the editor and `Ctrl+click` all tell them.

Note that in your original phrasing, `exits.north == "Foyer"` would be a type error: the map's value type is `ref<room>`, so the operand must be a room id (`corridor`), not a string. That the compiler catches it is the point of declaring the type.

**Restrictions, each with a reason:**

1. **Keys must be writable as identifiers.** `map<identifier, V>` and `map<enum, V>` work; `map<int, V>` and `map<string, V>` cannot use dot syntax, because §3.3 identifiers may not begin with a digit or contain arbitrary characters. Those use `map_get` (§10.6.1).
2. **Keys must be literal.** A key computed at run time cannot be written in a path. That also uses `map_get`.
3. **A missing key yields `none`**, and this is the rule that makes the syntax safe.

#### 6.6.2 A missing key is `none`, but a missing property raises

These look identical and behave differently, so the distinction is worth stating plainly:

| Access | Absent means | Result |
|---|---|---|
| `noun.damage` — a **property** | the *type* does not declare it | compile error, or a runtime raise (§8.8.4) |
| `location.exits.north` — a **map key** | the *contents* do not include it | `none` |

The asymmetry is principled rather than convenient. Whether a property exists is a **static** question about the type, so an absent one is a defect. Whether a key is present is a **runtime** question about contents — a room with no north exit is not a defect, it is Tuesday. Raising there would make the common case the error case.

So a `map<K,V>` read has type `V` *or* `none`, and the idiomatic emptiness test is direct:

```stardata
conditions = { location = { exits.north == none } }
```

Two consequences:

- **Chaining through a missing key raises.** `location.exits.north.name` where there is no north exit is an error, not silently `none`. Silent propagation would hide the mistake several lines from its cause, which §8.8.4 already refuses for properties. Test the key first, or use `value_or` (§10.6.1).
- **`includes` with a `key` argument** tests presence without reading: `{ includes = { collection = location.exits  key = north } }` (§6.5.1).

#### 6.6.3 The rule that resolves the ambiguity

> **A bare identifier in an *argument* position is always a global or const. It never resolves against an enclosing object scope.**

So in your example, the second line is a global and the first must say what it means:

```stardata
conditions = {
    noun = { includes = { collection = noun.waypoints  value = foo } }
    includes = { collection = seen_endings  value = good_end }
}
```

The `noun.` looks redundant inside `noun = { … }`, and that redundancy is the price of the guarantee. It buys: the meaning of an argument never depends on where the statement sits, moving a condition between blocks cannot change what it does, and a reader can resolve any reference without scanning outward.

This is distinct from — and does not disturb — the existing rule for **key** positions:

| Position | Bare identifier means |
|---|---|
| **Key**, inside a namespace block (`noun = { open == yes }`, `global = { alert_level == high }`) | a member of that namespace |
| **Argument** (`collection = …`, `value_of = …`, `default = …`) | a `global` or `const` |

The two are always distinguishable, because a key is followed by an operator and an argument is not.

#### 6.6.4 Namespaced ids

Libraries prefix their globals (`starscape.combat_round`, §6.4), which looks exactly like a path. Resolution is therefore ordered and deterministic:

1. Try the whole identifier as a `global` or `const` id.
2. Failing that, split at the last dot and resolve as `<object-or-slot>.<property>`.
3. If **both** would resolve, that is an error naming both candidates. It requires a rename, and it should be vanishingly rare — an author would need a global literally called `noun.waypoints`.

---

## 7. The schema layer

### 7.1 Purpose

Every top-level form and every key within it is described by a **schema**. Schemas are themselves written in Stardata — the core-owned ones in `libs/starcore/builtin/` (§7.2.2), the rest in the standard library — and libraries MAY contribute more. The schema layer serves three purposes simultaneously, and this is the main reason it exists rather than the validation being hard-coded:

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
| `type_of` | identifier | *a dependent type.* Names a sibling key of the same form whose **value** is this key's type. Exactly one of `type` and `type_of` is written; declaring both is an error |
| `required` | bool | default `no` |
| `arity` | `one` \| `many` | default `one` (§5.3) |
| `default` | scalar | value if absent |
| `combine` | `override` \| `merge` \| `append` \| `smart` | §5.4.2; default `override` |
| `unique_in` | identifier | the namespace within which the value must be unique |
| `doc` | text | documentation string |
| `editor` | identifier | a hint for the inspector widget (`text_area`, `object_picker`, `slider`, …) |
| `deprecated` | text | if present, using this key produces a warning carrying this message |
| `exclusive_group` | identifier | this key belongs to a mutually exclusive group; see §7.2.1 |
| `stage_order` | list of identifiers | *on the schema, not a key.* The ordered stages of this form, through which type narrowing flows (§8.8.3). Declaring it keeps the narrowing analysis free of any knowledge of what the stages are. A form declaring none has no stages, and narrowing within it does not flow between keys |

**`type_of`, the dependent type.** Most keys have a type the schema knows in advance. A few do not, because their type is decided by *other data in the same block*:

```stardata
key = { name = type     type = type_expr }
key = { name = initial  type_of = type }
```

That is §6.4's `global`. A global's starting value is an `int` for one global and a `map<direction, ref<room>>` for the next, so no fixed `type =` on the `initial` line could be right for both. `type_of = type` says "check this against whatever the `type` key beside it holds", and the ordinary value check does the rest.

The alternative — a type meaning "somebody else checks this" — was tried and removed. It answered the question for one key by declining to answer it for every key in every schema in the program, which is an escape hatch, not a type.

The sibling named MUST be a key of the same form; naming one that is not is an error at the schema. If the sibling is absent from a particular block, this key is not checked — its own `required` (or the exclusive group it belongs to) is what reports that, in the author's terms.

Two further users of it are specified: §11.1's `set = { target  prop  value }`, where `value` is typed by the property `prop` names, and `set_global = { id  value }`, where it is typed by the global.

#### 7.2.1 Exclusive groups

Several forms accept one of two alternative arguments and never both. Earlier drafts stated this in prose and gave the schema no way to express it, which meant the rule was unenforceable:

| Form | Alternatives |
|---|---|
| `rule` | `of_action` / `of_event` (Appendix C.1) |
| `list_remove` | `value` / `index` (§6.5) |
| `present_in` | a list block / a `where` query (§8.6) |
| `includes` | `value` / `key` (§6.5) |

```stardata
key = { name = of_action  type = ref<action>  exclusive_group = subject }
key = { name = of_event   type = identifier   exclusive_group = subject }
```

Within a block, **exactly one** key of a given `exclusive_group` may appear. Zero is an error if any member is `required`; two or more is always an error, and the diagnostic MUST name the group's members. A group MAY declare a `fix_hint` so the error can point at the right construct rather than merely refusing.

`block<S>` as a type means "a record block conforming to schema `S`", which is how nested shapes such as `rule`, `stage`, `node` and `choice` are declared.

### 7.2.2 Core-owned schemas

Some schemas are not the standard library's to define, because `starcore` reads and writes the data they describe. The containment tree, the class of an object, its sector, its presence set — these are not conventions the engine hopes a library will follow, they are the shape of its own data structures.

**Those schemas are owned by `starcore`, registered before any file loads, and `sealed`.** A library may extend them; it cannot redefine, retype or remove them.

This is a deliberate departure from "the standard library defines everything", and the reason is worth stating because the alternative is a known failure mode rather than a hypothetical one. ADRIFT 5 requires a library to create the location properties the system uses; Inform 7's compiler attaches special handling to the eighth action defined, expecting it to be Going. Both are cases of an engine *depending* on a convention while *pretending* the library is free. The result is a wart that nobody can see in the source and that breaks bewilderingly when violated.

> **If `starcore` requires something to be defined a particular way, it MUST assert that requirement rather than assume it will be met.**

Concretely, an implementation MUST reject:

- a `schema` declaration whose `id` names a sealed core schema;
- a `class_extension` that retypes an inherited core property, or that changes a core class's `of_class`;
- a `prop_def` that redeclares a core property name with a different type;
- the absence of anything core requires — reported at load, naming the requirement, rather than surfacing as a failure later.

Adding is always permitted, and **sealing prevents redefinition, not extension**: new keys on a core schema through `schema_extension` (§7.5), new properties on a core class through `class_extension` (§8.2), new subclasses, new traits.

*(An earlier draft named `provides_schema` here. That was simply wrong — `provides_schema` is a manifest field on `library`, not a mechanism. §7.5 is the mechanism.)*

#### 7.2.3 Markers, not magic names

Where the engine needs to know *which* property means something, the library says so with a **marker** rather than the engine hard-coding a name. `prop_def` therefore accepts a block as well as a bare type:

```stardata
trait = {
    id = openable
    prop_def = {
        open = { type = bool  affects_scope = yes }
    }
    open = no
}
```

`affects_scope` tells the scope cache (proposal §5.4) to invalidate when this property changes. The engine never learns the name `open`; it learns that *some* properties affect scope and is told which. A ruleset with a `shuttered` property gets the same behaviour by declaring the same marker.

This is the general form of the rule above: core depends on **declared, checkable markers**, and hard-codes a name only where the concept itself is core (§7.2.4). Defined markers include `affects_scope`, `always_resident` (§5.3 of the proposal), and `save_exclude`.

#### 7.2.4 Format forms and core-owned forms

An earlier version of this section asked one question — *does `starcore`'s own code read or write it?* — and used the answer for two different purposes. That conflated **who parses the declaration**, which happens at load, with **who reads the data**, which happens at run time. `global` is the case that breaks it: the format layer parses one and the engine reads it, and forcing a single answer put the reader in one library and the declaration in the other, where the two quietly drifted apart.

There are therefore three kinds of form, and two questions.

**Format forms** are parsed by the format layer itself. The membership rule is mechanical and testable:

> A form is a **format form** if and only if the format layer parses it with a reader of its own, rather than validating it generically against its schema.

That is exactly the set whose shape would otherwise be stated twice — once as a `schema` declaration and once as C++ — so it is exactly the set that needs a check that the two agree. `scripts/check_format_forms.py` is that check.

| Format forms | Why the format layer parses it |
|---|---|
| `schema`, `key` | the bootstrap: these are what read every other declaration, so they cannot be declared in one |
| `schema_extension` | §7.5 — it changes what a form is, before anything is validated against it |
| `class`, `trait`, `class_extension` | §8.1–§8.3 — the type graph that property resolution and instantiation checking walk |
| `enum` | §6.2 — `enum<E>` is a type, and types are the format's |
| `global`, `const` | §6.4 — declared, typed and checked at load; what one *means* is the engine's |
| `prop_def`, `prop_marker` | the parts of a class declaration |
| `resolve` (§8.3), `version_constraints` (§13.3) | nested shapes of a declaration the format layer reads. Neither has a reader yet: both are declared, and validated generically until one exists |
| `core_requirement` | §7.2.5 — the gate that runs before core is handed anything; **writing one is reserved to `starcore`** (§7.2.5.1) |
| `library` | §13.3 — packaging and load order, checked against the registry |

These are declared, as data, in `libs/stardata/builtin/format.star`, for §7.1's reason: validation, editor generation and documentation come from one source. The reader stays hard-coded, and a test asserts the two say the same thing.

**Core-owned forms** are validated generically like any other, and are core's because core's own code reads the data:

| Core-owned forms | Why |
|---|---|
| `action`, `rule`, `turn_hook` | the turn sequence and dispatch index |
| `sector` | residency and streaming |
| `project` | the game's own manifest: `start_room`, `player`, `entry_sector` |
| `style` | §9.3 — the text VM emits style spans, and `@style(id)` (§5.4.1) has an argument nothing could otherwise check |
| `loc` | §9.6 — the engine resolves every `$key` through it, and owns the fallback chain when one is missing |

**Everything else** is library policy.

*Reserved to core* (§7.2.5.1) is a third axis, independent of both. `core_requirement` proves it: only `starcore` may write one, and the form is the format layer's — because the format layer is what refuses everybody else, at load, before core sees the file at all.

| Core-owned classes and traits | Why |
|---|---|
| `starcore.object` | §8.1.1 — the containment tree, presence, identity |
| `starcore.room` | scope is computed from an actor's room; the `location` slot resolves to one |
| `starcore.actor` (trait) | the actor loop iterates these, and `busy_until` lives on them |

Everything else in `stdlib` — `thing`, `person`, `container`, `supporter`, `door`, `backdrop`, every action, every message — is ordinary Stardata with no privileged status, and could be replaced wholesale by a different library.

**[OPEN]** The exact membership of the second table is the part most worth arguing about. `starcore.room` and `starcore.actor` are included because the parser and turn loop are core and cannot function without the concepts. A narrower reading would make them markers instead (`is_location = yes`), at the cost of an indirection for two concepts that an IF system is never going to be without.

#### 7.2.5 `core_requirement` — how core asserts

§7.2.2's rule is only as good as its enforcement, so the requirements are **declared, not implemented in silence**. A `core_requirement` names one thing `starcore` depends on, and failing it is a diagnostic at load carrying that name.

```stardata
core_requirement = {
    id       = object_holder
    requires = property
    subject  = starcore.object
    member   = holder
    type     = ref<starcore.object>
    doc      = "Containment is a field of the world store; C++ reads it directly."
}
```

| `requires` | Satisfied when |
|---|---|
| `form` | a schema with id `subject` is registered, and sealed |
| `class` | a class with id `subject` exists, and is sealed |
| `trait` | a trait with id `subject` exists, and is sealed |
| `property` | `subject` declares `member`, at `type` if one is given |
| `parent` | `subject` derives from `member` |

- Requirements are checked **after all files load**, against everything loaded, and before any pass that depends on them.
- The diagnostic names the requirement's `id` and quotes its `doc`, so an unmet requirement reads as a sentence rather than as a missing symbol.
- Because they are checked against the whole program, they guard the built-in files against their own edits as much as against a library's: deleting a property from the core object model fails the build here, by name, rather than in a later phase when something reads it.

#### 7.2.5.1 Why this is core-only, and stays that way

It is tempting to generalise the form — to let any library assert what it depends on — and that would be a mistake. The name `core_requirement` is correct, not merely conventional, and the reason is structural rather than a judgement about how often it would be useful.

**A library's dependencies are already checked by being used.** A ruleset that needs a `weapon` class declares one, and everything that reads `weapon.damage` is statically checked against it (§8.8). If a game `@replaces` that class with an incompatible shape, every rule, template and condition touching the missing property fails at compile time, at the exact site that cares. A `requirement` restating "weapon must have damage" would add a second, weaker report of something the type system already catches precisely.

**Core has a boundary no library has.** `starcore`'s C++ reads the containment tree, the relation enum and the class table directly, and the schema layer cannot see C++. Nothing about `holder` being a `ref` is checked by anything, because the code that depends on it is not written in Stardata. `core_requirement` exists to bridge exactly that gap — and by §2.2 of the proposal, no library has such a gap, because a library is data and script all the way down.

So the rule is self-limiting in a useful way: **if a library ever appeared to need this mechanism, that would be evidence the library had C++ in it**, which the architecture does not allow. The form staying core-only is a constraint worth keeping rather than an asymmetry to apologise for.

Your framing of the asymmetry is the other half, and it is the practical one: a game *can* replace a library's class, and should be expected to understand what depends on it. A game cannot replace anything `starcore` does without forking the project and shipping a custom runtime — so core's invariants are not things an author can reason about, negotiate with, or be warned about. They have to hold, and the only way to know they hold is to assert them.

**One residual, recorded rather than solved.** A library whose behaviour lives in Lua reads properties dynamically (§12), so a hostile `@replaces` surfaces there at run time rather than at compile time. That is the accepted cost of the scripting escape hatch, and the answer is §8.8.4's catchable error naming the object, the property and the source location — not a declaration form that would only ever be used by the small number of libraries that are mostly script.

**`core_requirement` is therefore a reserved internal form.** It is core-owned (§7.2.4), sealed like every other core-owned form, and additionally **restricted in use**: a `core_requirement` declared by anything other than `starcore` — a library, a ruleset, or a game — MUST be rejected, naming this section.

The restriction is worth stating as a rule rather than leaving to convention, because the failure it prevents is the one this whole section is about. A library that could assert requirements would be asserting them *about other people's data*, at load, in core's voice, with no way for the author being refused to tell whose rule they had broken. That is the ADRIFT wart with the sign flipped: not an engine depending silently on a library, but a library conscripting the engine's authority. The form is core's because only core has a dependency the schema layer cannot otherwise see; anything else claiming one is either mistaken or overreaching, and both are worth a diagnostic.

### 7.3 Open and closed schemas

A schema is **closed** by default: an unknown key MUST be an error, with a "did you mean …?" suggestion computed by edit distance against the declared keys.

The suggestion is subject to two constraints, which apply wherever this document asks for one (§14.3 lists the other places):

- **It MUST be deterministic** (§14.1). Where two candidates are equally near, an implementation MUST prefer the one declared first — declaration order being load order (§13.2) — rather than the alphabetically first or whichever a hash table produced. An author comparing two builds must get the same advice from both.
- **It MUST be withheld when no candidate is near.** A suggestion offered for a name that resembles nothing reads as knowledge the checker does not have. What counts as near is an implementation's to choose; a distance of at most a third of the longer name is the recommended bar.

A candidate differing from what was written only in letter case SHOULD be suggested regardless of distance, since case is the one difference edit distance measures badly.

A schema MAY declare `open = yes`, permitting unknown keys, which are retained and made available to scripts. This is intended for author-defined metadata and SHOULD be rare.

### 7.4 Object instantiation forms

A statement whose key is the id of a declared **class** instantiates an object of that class:

```stardata
room = { id = your_cell  exits = { north = corridor } }
```

The class name is on the left and the object's id inside. This is deliberate and MUST NOT be reversed: it is what allows the schema layer to dispatch on the left-hand key, it groups a file by kind for scanning and outlining, and it makes every top-level statement uniform in shape.

The keys permitted inside an instantiation block are those of the class's property set (§8), plus the universal keys `id`, `traits`, `in`, `on`, `part_of`, and `sector`.

### 7.5 `schema_extension`

A library adding a key to an existing form needs a mechanism, and §7.2.2 previously pointed at a manifest field that is not one. `schema_extension` mirrors `class_extension` (§8.2) and reads the same way:

```stardata
schema_extension = {
    of_schema = action
    key = { name = stamina_cost  type = int  default = 0 }
    key = { name = combat_style  type = ref<combat_style> }
}
```

Semantics:

- **Adds keys only.** A key whose `name` already exists is a redefinition, not an extension: identical redeclaration is a warning (redundant), any difference is an error. This mirrors §8.7's rule for object-local `prop_def`.
- **Works on sealed schemas.** This is the point of the distinction in §7.2.2 — a ruleset may add `stamina_cost` to the core `action` form, and may not change what `id` means.
- **Applies in load order**, after the base schema is registered. Extending a schema that does not exist is an error naming it, rather than quietly creating one.
- **Changing an existing key is out of scope.** Altering a default belongs in the project's `defaults` (§13.1); altering anything else means replacing the whole schema, which §7.6 covers and sealing forbids.
- An extension MAY add a key to an existing `exclusive_group` (§7.2.1); the group's check then applies to the merged set.

### 7.6 Duplicate declarations and `@replaces`

A `unique_in` key (§7.2) already makes two declarations of the same id an error. What was missing is the deliberate case: an author who wants the standard library's `take` to behave differently has no way to say "replace that one", and no mechanism at all for forms that lack a `class_extension` equivalent.

**No declaration may be duplicated.** For any form whose schema declares a `unique_in` key, two declarations sharing that key's value are an error citing both spans. (`rule` and `loc` are unaffected: neither declares a unique id, so several are normal.)

**`@replaces` is how a later declaration supersedes an earlier one:**

```stardata
action = @replaces(stdlib) {
    id    = take
    match = { "get/take/grab [something]" }
    # ...a complete declaration; nothing is merged from the original
}
```

- The argument names the **library id** whose declaration is being superseded, not a file. The project's own id is legal.
- **It is an error if there is no such declaration from that source.** This is the whole value of naming it: a typo, or an upstream rename, or a library that stopped shipping the thing you were patching, all become build failures instead of a silently-new declaration that never takes effect.
- Replacement is **total**. The new declaration stands alone. Merging is what `class_extension` and `schema_extension` are for, and conflating the two is how you get declarations that are half one thing and half another.
- A library may only replace something loaded **before** it (§13.2).
- **`@replaces` on a sealed declaration is an error.** That is precisely what sealing means (§7.2.2): extend freely, never supersede.

`@replaces` is deliberately not spelled `@override`. §5.4.1's `@override` combines a *value within a key*; this supersedes a *whole declaration*. Reusing the word would make two quite different operations look alike.

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
- `prop_def` declares properties. A `prop_def` block maps a property name either to a bare type or to a block carrying markers (§7.2.3).
- Any other key sets the class's **default value** for that property. The property MUST be declared, by this class or an ancestor.
- A class with no `of_class` derives from `starcore.object` (§8.1.1). There is no way to declare a class outside that hierarchy.

#### 8.1.1 `starcore.object`, the built-in root class

Every world object is a `starcore.object`, in the way that every C# or Java type is an `Object`. It is **built into `starcore` and cannot be redefined** (§7.2.2), because its properties are not conveniences — they are the fields of the world store (proposal §5.2) under author-visible names.

| Property | Type | Purpose |
|---|---|---|
| `holder` | `ref<starcore.object>` | the containment parent; `none` for a root |
| `relation` | `enum<relation_enum>` | how it is held — `in`, `on`, `under`, `behind`, `carried`, `worn`, `part_of` |
| `sector` | `ref<sector>` | residency (§8.6.2 of the proposal) |
| `present_in` | `set<ref<starcore.room>>` | presence, for objects in several places (§8.6) |
| `name` | `text` | what the parser matches and the templates print |
| `synonyms` | `list<identifier>` | additional parser names |

Declaring these in one built-in place is what lets `starcore` implement predicates like `held_by`, `carrying` and `containing` in C++ against a known layout, rather than reading whatever a library happened to call its parent pointer. The alternative — the engine walking library-defined properties by convention — is both slower and the ADRIFT failure mode described in §7.2.2.

A library MAY add properties to `starcore.object` with `class_extension`; it MUST NOT retype or remove these.

**Being the root is declared, not assumed.** A class declaration MAY carry `root = yes`:

```stardata
class = {
    id     = starcore.object
    root   = yes
    sealed = yes
    prop_def = { ... }
}
```

A class with no `of_class` descends from whichever class declares it. **At most one class in a program may declare `root`**, and declaring a second is an error naming both; a `trait` may not declare it, and neither may a class that also declares an `of_class`.

The name `starcore.object` is therefore core's, in data, while the *concept* of a root is the format's. That is what lets the format layer resolve a property through the whole chain — including the last link — without naming any class. Before this it took the root's name as a parameter threaded through three signatures, and two of the walks in the format layer disagreed about whether to follow it.

### 8.2 Class extension

```stardata
class_extension = {
    of_class = room
    prop_def = { condition = condition_enum }
    condition = breathable
}
```

`class_extension` modifies an existing class in place: it adds properties and changes defaults for a class declared elsewhere, including in a library the author cannot edit. It MUST NOT change what it extends — writing a second `of_class` or `of_trait` is an error.

**A trait is extended the same way**, through `of_trait`:

```stardata
class_extension = {
    of_trait = openable
    prop_def = { open_sound = resource }
}
```

`of_class` and `of_trait` are an exclusive group (§7.2.1): exactly one of them is written. Naming both, or neither, is an error.

The two keys exist rather than one because §8.3 gives classes and traits separate namespaces, so a single id may legally name both. A lookup that tried one and fell back to the other would extend whichever the load order happened to reach first — silently, and differently between runs. Naming `of_class` where the id is a trait is therefore an error, and the diagnostic MUST say which namespace was searched rather than merely reporting that nothing declares the name.

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
thing = { id = brass_key      in      = ornate_box }
thing = { id = tarnished_mug  on      = mess_table }
thing = { id = access_panel   part_of = reactor_console }
```

**This is sugar.** Each relation name is shorthand for setting the two `starcore.object` properties of §8.1.1, so the first line above means exactly:

```stardata
thing = { id = brass_key  holder = ornate_box  relation = in }
```

Both spellings are legal and produce identical data. The sugar exists because placement is written for nearly every object in a game and the long form would be noise; the long form exists because it is what `holder` and `relation` actually are, and because a computed or conditional placement has no relation keyword to use.

Writing a relation keyword *and* `holder`/`relation` in the same block is an error — they are the same two slots, and the conflict is not resolvable by precedence.

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
| `location` | the acting actor's current room — `room` |
| `actor` | `person`, or whatever the ruleset narrows it to |
| `noun`, `second` | determined by the action's grammar token (proposal §6.2) |

Grammar tokens carry some of this information: `[something]` yields `thing`, `[class:weapon]` yields `weapon`.

The names in that table are the standard library's, and an implementation MUST NOT assume them: §7.2.4 makes `thing`, `person` and `room` replaceable, so a compiler that hard-coded them would be depending on a library it also claims is optional. Absent a declaration from the ruleset, the static type an implementation is entitled to assume is the **root class** for `[something]` and its relatives, and for `actor`, `self` and `speaker`; `location` resolves to the core room class. A narrower token — `[class:weapon]` — names its own class and is assumed exactly.

The consequence is deliberate and matches the advice below: a broad token yields broad static knowledge, and narrowing it is the author's to do where the meaning is, in a restriction that also produces a message. **[OPEN]** There is no spelling yet by which a ruleset declares that its `actor` slot is a `person`; until there is, every read on those slots is narrowed at the point of use.

**But narrowing is a poor reason to choose a narrow token, and an earlier draft of this section gave the opposite advice.** A token controls three separate things, and conflating them produces bad games:

| A token controls | `[something]` | `[class:drinkable]` | `[something preferably drinkable]` |
|---|---|---|---|
| what it **matches** | anything in scope | only drinkables | anything in scope |
| what it **prefers** when ambiguous | nothing | — | drinkables |
| what the compiler may **assume** | `thing` | `drinkable` | `thing` |

If `drink` matches only `[class:drinkable]`, then `DRINK LAPTOP` does not fail *in the world* — it fails **in the parser**, and the player gets "you can't see any such thing" about a laptop that is plainly on the desk. Recovering the sensible refusal then means writing a second `drink [something]` action purely to catch the misses, which duplicates the verb and puts the interesting judgement in the wrong place.

**Restrictions are the right place for that judgement, and they narrow just as well** (§8.8.3). One action, a real message, and full static knowledge afterwards:

```stardata
action = {
    id    = drink
    match = { "drink/sip/swallow [something]" }
    restrictions = {
        noun = { has_trait = drinkable
                 failureMsg = "[The noun] [is noun] not something you can drink." }
    }
    # Past the restriction, `noun` is statically known to be drinkable.
    effects = { add_global = { id = hydration  amount = noun.volume_ml } }
}
```

**So: prefer the broad token.** A narrow token is right only where a non-match should genuinely be unparseable rather than refusable — `[direction]`, `[topic]`, and grammar lines distinguished by their own literal words. Where a class matters merely for choosing between candidates, `[something preferably drinkable]` expresses that without making anything unparseable.

#### 8.8.2 Three static answers

For a read of property `P` on a slot of static type `T`, the compiler distinguishes:

- **Definitely present** — `T` or an ancestor or trait of `T` declares `P`. Compile to a direct access.
- **Definitely absent** — nothing in the program that could satisfy `T` declares `P`. **Error.** This is the case that catches typos, and it is the common one.
- **Possibly present** — some objects satisfying `T` declare `P` and others do not. The author must resolve it; see below.

#### 8.8.3 Narrowing — the decision

**[DECISION]** Both of the options you posed, layered, with static narrowing as the primary mechanism and a runtime escape that must be written out.

A "possibly present" read MUST be justified by one of:

**1. A narrowing condition earlier in the same conjunction.** Because the condition language is ordered and short-circuiting (§10.1), `of_class`, `has_trait` and `is` narrow the slot's static type for everything after them.

Narrowing also flows **forward through the pipeline stages**, because each stage gates the next. The sequence is not built into the analysis: a schema declares its own `stage_order` (§7.2), and the dataflow runs over whatever it finds — so the implementation of narrowing knows none of the stage names below (proposal §2.1.1).

| A narrowing in… | …narrows |
|---|---|
| `when` | `conditions`, `restrictions`, `effects`, messages |
| `conditions` | `restrictions`, `effects`, messages |
| `restrictions` | `effects`, messages |

The last row is what makes §8.8.1's advice work: a restriction that requires `has_trait = drinkable` either fails and aborts the action, or passes — in which case the noun *is* drinkable by the time effects run, and the compiler knows it. An author gets a good failure message and full static knowledge from the same three lines, with no second action and no runtime check.

```stardata
rule = {
    of_action = examine
    when      = { noun = { of_class = weapon } }      # narrows noun to weapon
    successMsg = "It is rated for [noun.damage] damage."   # now legal
}
```

Rules are the other half of the same idea: where a broad action needs different behaviour per class, that difference belongs in a rule keyed on `when`, not in a second action with a narrower grammar token.

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
        value_or = { datum = noun.damage  default = 0 }
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

**A missing map key is not this case.** Map contents are runtime state, so a key that is not present yields `none` rather than raising; see §6.6.2 for why the two differ.

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

Literal text is everything not otherwise matched. `\[`, `\]`, `\$` and `\@` produce those characters literally.

Those escapes are the **string** escapes of §3.5, not a second escape layer belonging to this grammar, and the difference decides where a template may be parsed from. By the time a string literal's value has been decoded, `\[` and `[` are the same byte and the rule above is unenforceable — so an implementation MUST parse a template from the literal's source text. The same reading gives every diagnostic in this section a span in the author's own bytes rather than an offset into a reconstructed string.

`Conditional` is written above as a nested production and MUST NOT be parsed as one. `[end]` closes an explicit tooltip span (§9.4) as well as a conditional, and which of the two a given `[end]` belongs to depends on whether the function that opened it is span-opening — a fact about the builtin table, which a library may extend. A conforming parser therefore produces the fragments in the order written and pairs them where that table is known.

A template MAY be written across adjacent literals (§3.5.1), and those are one template rather than several: a conditional may open in one literal and close in another.

### 9.2 Expressions

```ebnf
Expr      ::= Apply | Call | Path | Slot | Literal
Call      ::= Identifier '(' ( Expr ( ',' Expr )* )? ')'
Apply     ::= Identifier Expr
Path      ::= Slot ( '.' Identifier )+
Slot      ::= 'actor' | 'noun' | 'second' | 'self' | 'player' | 'speaker'
            | 'location' | Identifier
```

Slots are bound by the evaluation context: an action's message binds `actor`, `noun` and `second`; an object's own `description` binds `self`; a dialogue node binds `speaker`.

A function named in a template MUST resolve at compile time to either a **template builtin** (evaluated by the stack machine, never entering Lua) or a Starscript function (§12). Unresolvable names MUST be a compile error. Since the second half of that set is Starscript's, the check is only meaningful once §12 is implemented: an implementation without it MUST NOT report a subset of the condition, because a diagnostic that can flag a misspelled builtin but not a missing script function tells an author the names were checked when they were not.

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
- Keys MUST be unique within a language **and within one file**. A duplicate MUST be an error citing both spans.
- Across files, a later `loc` entry supersedes an earlier one for the same key and language, which is what load order means everywhere else in the format (§13.2). This is what makes a library's default message a default: `stdlib` declares `opened_default`, and a game that wants its own writes its own rather than being told it has collided with the library it is built on. Two entries in one table are an ambiguity nothing can resolve; two files disagreeing are ordered.
- A `$key` that no `loc` table defines, in any language, MUST be an error with a suggestion. "In any language" is the fallback chain above read forwards: a key present in the source language and missing from a translation is what that chain exists to survive, and reporting it would make adding a language an error rather than a partial translation.
- A declared key that nothing references SHOULD be a warning. Inline strings are assigned generated keys, so a key written out by hand exists in order to be referenced; one that is not is usually a renamed reference or a string that moved, and either way a translator is being asked to translate a line no player will see.

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

A key naming an object slot (`actor`, `noun`, `second`, `self`, `speaker`, `location`) or an object id opens a block of conditions about that object:

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
| `includes` | `{ includes = { collection = <datum>  value = … } }` or `{ … key = … }` — §6.5.1 |
| `is_empty` | `{ is_empty = <datum> }` |
| `quest_state` | `{ quest = … state = unstarted\|active\|complete\|failed }` |
| `quest_flag` | `{ quest_flag = captain_confronted }` |
| `said_before` | `{ said_before = kira_i_station }` |
| `attitude` | `{ npc = … toward = … at_least = 40 }` |
| `faction_alert` | `{ faction_alert = high }` |
| `dead` | `{ dead = captain_reyes }` |
| `random_chance` | `{ random_chance = 25 }` — percent, from the world RNG |
| `any_actor` | `{ any_actor = { in_combat == yes } }` |
| `script` | `{ fn = … }` |

Libraries add predicates by declaring them; the list above is `stdlib` plus the entries `stdlib/starscape` contributes (`attitude`, `faction_alert`, `dead`, `any_actor`).

### 10.5 `failureMsg`

When a **restriction** fails, the action has definitely been attempted and the player is definitely owed an explanation. A restriction that fails without a message is not a neutral omission — it forces the engine onto a generic fallback ("You can't do that right now."), which is the single most common way a parser game feels unfinished.

Accordingly: in a restriction context, a condition block **SHOULD** carry a `failureMsg`, and an implementation SHOULD warn about any restriction with no reachable message (§15). The word is SHOULD rather than MUST only because §10.5.3's inheritance chain often supplies one from an enclosing scope.

Two cases are outside that warning, and an implementation SHOULD NOT report either. An **empty** `restrictions` block is §5.4.2's `smart` override — "this action has no restrictions" — and has no failure to explain. And a restriction whose only message is **unreachable** has already been reported as such (§10.5.1); the author wrote the explanation, so telling them it is missing as well describes one mistake as two, and moving the message answers both.

Which condition stages are restriction contexts, and which are silent, is stated in this section rather than declared in the schema. An implementation therefore has nothing to say about a condition stage on a form this document does not describe: it MUST still check reachability there, since §10.5.1 is a fact about combinators rather than about stages, and MUST NOT assume the stage is silent — treating an unrecognised stage as silent would reject a correct message, which is the more expensive way to be wrong.

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

Every reader takes a `<datum>` (§6.6), so one reference notation serves globals and object properties alike.

| Reader | Produces |
|---|---|
| `value_of = <datum>` | the datum's value |
| `value_or = { datum = <datum>  default = D }` | as above, yielding `D` when the property is absent (§8.8.3) |
| `count_of = <datum>` | element count of a collection, as `int` |
| `at = { collection = <datum>  index = N }` | the element at `N`; the collection's element type |
| `map_get = { collection = <datum>  key = K }` | the value stored under `K`, or `none`. `a.b` desugars to this (§6.6.1), so `map_get` is what remains for **computed** keys and for key types that cannot be written as identifiers |
| `script_value = { fn = F }` | a script's return value, for anything the declarative readers cannot express |

`value_of` replaces what earlier drafts split into `global_value` and `prop_of`. Globals had accumulated three unrelated access syntaxes — a `global = { … }` namespace block, a `global_value` reader, and the `flag_set` sugar — and collapsing the value-reading cases onto one datum-taking reader removes two of them.

Note that `script_value` is distinct from the `script` predicate (§10.4): `script` returns a boolean and is a condition in its own right; `script_value` returns a value to be compared.

#### 10.6.2 Operands

The right-hand side of a `value` statement may be a literal scalar, a `const` id, or a `global` id. It may **not** be a second reader — comparing two computed values is a job for `script_value`, and allowing it here would mean nesting readers inside operands for a case that rarely arises.

```stardata
restrictions = {
    compare = {
        value_or = { datum = noun.damage  default = 0 }
        value > 3
        failureMsg = $too_feeble
    }
}

conditions = {
    # a collection held as a property of an object
    compare = {
        at = { collection = navcomp.waypoints  index = 0 }
        value == docking_gantry
    }
    # ...and one held as a global
    compare = {
        count_of = seen_endings
        value >= 1
    }
    compare = {
        value_of = core_temp
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
| `list_add` / `list_remove` / `list_clear` | `{ collection = <datum>  value = … }` or `index = …` (§6.5, §6.6) |
| `map_put` / `map_remove` | `{ collection = <datum>  key = …  value = … }` |
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

Actions declare a `duration` in ticks; `default` takes the project's default (60 in `stdlib`). Actors carry a `busy_until` tick and are skipped in the actor loop until the clock reaches it.

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

    # Load order: stdlib/stdlib is implicit and always first.
    uses = { starscape }

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

1. `stdlib`
2. libraries named in `uses`, in declaration order
3. the project's own sources, in a deterministic traversal: `project.star` first, then remaining files sorted by path
4. mods, in the player's configured order (runtime only)

Within a single file, source order governs.

**A source is loaded as a whole, not file by file.** Every declaration in one of the four groups above is registered before any of that group's contents are validated, so a reference may name something declared later in the same file, or in a file that sorts after it. The asymmetry between groups is deliberate and is the ordering above: a project may name what its libraries declared, and a library MUST NOT name what a project will.

An implementation MUST NOT make load order depend on filesystem enumeration order, since that differs between platforms and would make builds irreproducible.

### 13.3 Libraries

```stardata
library = {
    id = starscape
    version = "1.0.0"
    display_name = $lib_starscape_name
    requires = { stdlib >= "1.0.0" }
    uses_editor_feature = { rpg quests dialogue }
    provides_schema = { stat_block combat_style loot_table }
}
```

`provides_schema` is a **manifest**, listing the new top-level forms this library contributes so that the editor's library browser and a reader can see them in one place. It declares nothing and creates nothing; a mismatch against the schemas the library actually declares is a warning. Adding keys to an *existing* form is `schema_extension` (§7.5), which is a different operation and was at one point wrongly described here.

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
| A top-level statement naming neither a declared form nor a declared class (§7.2, §7.4) | error, with a suggestion |
| A type expression naming no builtin type, declared `enum`, form or class (§6.2) | error, with a suggestion |
| Unknown annotation (§3.8), or one §15 reserves | error |
| Two combining annotations on one value — `@before` `@after` (§5.4.1) | error, citing both |
| An annotation on a value its "Applies to" column excludes (§5.4.1) | error |
| An annotation whose arguments are not what it takes (§3.8, §5.4.1) | error |
| `@platform` naming a frontend the specification does not list (§5.4.1) | error |
| `==` in a binding context (§3.6) | error |
| The removed `?=` operator (§6.3.1, §15) | error, naming the removal and suggesting `=` |
| Bare `=` in a condition context (§3.6) | warning |
| `true` / `false` used as a value (§3.8) | error |
| Type mismatch against the declared type (§6.2) | error |
| Unresolvable `ref<C>`, or a reference to the wrong class | error |
| Trait property conflict without `resolve` (§8.3) | error |
| Undeclared global named by `set_flag` / `clear_flag` / `flag_set`, or one not of type `bool` (§6.4.1) | error |
| Reference to an undeclared `global` or `const` (§6.4) | error |
| A `<datum>` resolving neither as a global/const id nor as an object path (§6.6) | error |
| A `<datum>` resolving **both** ways (§6.6.4) | error, naming both candidates |
| A path segment whose intermediate is not `ref<C>`-typed (§6.6) | error |
| A map key in a path that is not a valid member of the key's type — `exits.nrth` for `map<direction, …>` (§6.6.1) | error, with a suggestion |
| Dot syntax used on a map whose key type cannot be written as an identifier (§6.6.1) | error, suggesting `map_get` |
| `includes` with a `key` argument on a non-map collection (§6.5.1) | error, naming the collection's declared type |
| `includes` with both `key` and `value` (§6.5.1) | error, suggesting the path form or `map_get` |
| Two or more keys of one `exclusive_group` in a block (§7.2.1) | error, naming the group's members |
| Two declarations sharing a `unique_in` value, without `@replaces` (§7.6) | error, citing both spans |
| `@replaces` naming a source that declared no such thing (§7.6) | error, with a suggestion |
| `@replaces` on a sealed declaration (§7.2.2, §7.6) | error |
| `schema_extension` redeclaring an existing key with a different declaration (§7.5) | error |
| `schema_extension` redeclaring an existing key identically (§7.5) | warning |
| `schema_extension` naming a schema that does not exist (§7.5) | error |
| `class_extension` naming neither `of_class` nor `of_trait`, or both (§8.2, §7.2.1) | error, naming the group's members |
| `class_extension` naming a class or trait that does not exist (§8.2) | error; where the id is declared in the *other* namespace, the diagnostic MUST say so and offer the other key |
| A second class declaring `root = yes` (§8.1.1) | error, citing both spans |
| `root = yes` on a `trait`, or on a class that also declares `of_class` (§8.1.1) | error |
| A `key` declaring both a `type` and a `type_of`, or neither (§7.2) | error |
| `type_of` naming a key the form does not have (§7.2) | error, with a suggestion |
| An unmet `core_requirement` (§7.2.5) | error, naming the requirement and quoting its `doc` |
| A `core_requirement` declared by anything but `starcore` (§7.2.5.1) | error, naming the section |
| An unknown marker in a `prop_def` block (§7.2.3) | error, listing the markers |
| A relation keyword and `holder`/`relation` in one block (§8.5) | error, citing both |
| `provides_schema` not matching the schemas a library declares (§13.3) | warning |
| No key of a required `exclusive_group` (§7.2.1) | error |
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
| Unbalanced brackets in a template (§9.1) | error |
| Undeclared style name (§9.3), with a suggestion | error |
| Duplicate localisation key within one file and language (§9.6), citing both spans | error |
| A `$key` no `loc` table defines, in any language (§9.6), with a suggestion | error |
| A declared localisation key nothing references (§9.6) | warning |
| `-=` removing an absent entry (§6.3) | warning |
| Two `class_extension`s setting the same default (§8.2) | info |
| Project crossing 64 declared traits (§8.3) | info |

---

## 15. Reserved for future use

The following are reserved and MUST currently be rejected, so that adding them later is not a breaking change:

- The characters `[` and `]` outside string literals. They are reserved exclusively for the template language and parser grammar tokens, and MUST NOT acquire a block meaning.
- The operators `*=`, `/=`, `=>`, `->`, `::`.
- The operator `?=`, which earlier drafts specified and §6.3.1 removed. It MUST be rejected with a diagnostic naming the removal and suggesting `=`, rather than as an unknown character — a file written against an old draft deserves to be told what happened.
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
| A36 | `@debug` resolves at compile time, `@platform` at run time; a set of disjoint `@platform` alternatives is **one** binding (§5.4.1) | §5.4.1 previously called both "conditional presence" and said a statement either removed "behaves as though it had not been written". That is true of `@debug`, whose stripping produces a release build; it cannot be true of `@platform`, because one `.spak` is signed and shipped for every frontend and the frontend declares itself at session start (proposal §12.2). Compiling `@platform` away would mean one artefact per frontend |
| A37 | Overlapping `@platform` alternatives are a duplicate key; there is no fallback precedence (§5.4.1, §5.3) | Under a shared frontend the engine would hold two candidates and no rule to pick. Making the unannotated binding a silent default would put the answer in a precedence rule rather than in what the author wrote, which is the failure mode `@override` versus a bare value already avoids |
| A38 | The five combining annotations are mutually exclusive; at most one per value (§5.4.1) | §5.4 required "contradictory combinations" to be rejected and gave `@before @after` as the example without saying what made it contradictory. Each of the five answers one question, so a second is not a refinement but a second answer |
| A6 | `none` as a distinct value from `inherit` (§5.5) | "Explicitly empty" and "unchanged" are different, and conflating them makes clearing a reference impossible |
| A7 | `string` type distinct from `text` (§6.2) | Machine-facing values should not be localisable or interpolatable |
| A40 | A template is parsed from the literal's source text, not from the decoded string (§9.1) | §3.5 makes `\[`, `\]`, `\$` and `\@` string escapes, so decoding happens first and leaves an escaped bracket indistinguishable from a real one. §9.1's escape rule is only implementable one way round |
| A41 | Localisation keys are unique within a file; a later file supersedes an earlier one (§9.6) | §9.6 said "unique within a language" without naming a scope, and the widest reading forbids a game from overriding a library's default message — which the `_default` suffix on every one of `stdlib`'s presumes is possible. Two entries in one table are ambiguous; two files are ordered (§13.2) |
| A42 | `style` and `loc` are core-owned forms, not `stdlib`'s (§7.2.4, Appendix C) | §7.2.4's test is whether `starcore`'s own code reads or writes the form, and the text VM does both. A `style` core could not name would leave `@style(id)` with an argument nothing checks; a `loc` core could not read would leave §9.6's fallback chain with nothing to fall back through |
| A43 | An empty `restrictions` block, and a restriction whose only message is unreachable, are both outside the missing-message warning (§10.5) | The first has no failure to explain; the second has an explanation in the wrong place, and reporting it twice describes one mistake as two |
| A8 | `@style(name)` spans to the next style directive or end (§9.3) | Matches the proposal's example; the alternative (explicit closing) is noisier |
| A9 | Missing localisation renders as `«key»` rather than blank (§9.6) | A missing string must be visible in play |
| A10 | Condition evaluation short-circuits in source order (§10.1) | Observable because conditions may call scripts, so it must be specified rather than left to the implementation |
| A11 | `failureMsg` in a silent context is an error (§10.5) | It would otherwise be a message the author writes and never sees |
| A12 | Load order sorts by path rather than filesystem order (§13.2) | Reproducible builds across platforms |
| A13 | `[` and `]` reserved against ever becoming block syntax (§15) | Guards the v0.2 decision to drop the ordered/unordered distinction |
| A14 | Juxtaposition as single-argument application (§9.2.1) | Message text is what authors write most; `[the noun]` reads better than `[the(noun)]`. Stated as one general rule so there is no special-cased article syntax |
| A15 | Capitalisation by initial letter, resolved generically (§9.2.2) | Avoids a capitalised twin for every builtin, and extends to author-defined functions for free |
| A44 | ~~`any` as a declared type, for a value whose type is another key's value~~ — **superseded by A48** | It answered the question for one key by declining to answer it for every key in every schema in the program. Any third-party schema could have opted out of type checking by writing one word, and nothing would have reported it. A dependent type (§7.2's `type_of`) says the same thing about the one key that needs it |
| A45 | §7.2.4's ownership test splits into two questions: who parses the declaration, and who reads the data (§1.2.1, §7.2.4) | The single test forced one answer for both, and `global` needs two — parsed by the format layer, read by the engine. The consequence was a reader in one library and a declaration of the same form in the other, which drifted: `class` declared a `traits` key nothing read for nine tasks. **Format forms** are now a named category with a mechanical membership rule, and a CI check that the two statements agree |
| A46 | The root class is declared with `root = yes`, not assumed by name (§8.1.1) | The format layer resolved properties through a chain whose last link was a class name passed in as a parameter — and two of its own walks disagreed about whether to follow it. Declaring the root keeps the *name* core's while making the *concept* the format's, which is what let the two walks become one |
| A47 | `class_extension` extends a trait through `of_trait`, rather than a separate `trait_extension` form (§8.2) | One identifier is all that would have differed between the two forms. An exclusive group (§7.2.1) states the "exactly one" rule in the schema, where an author can read it. A single `of =` was rejected because §8.3 gives classes and traits separate namespaces, so one id may name both and the lookup order would decide silently |
| A48 | `type_of` — a key's type may be the value of a sibling key (§7.2, §6.4) | §6.4's own examples give a global a `set` and a `map` as its initial value, which `scalar` rejects and no fixed type admits. Naming the sibling scopes the escape to the one key that needs it, and keeps the rule in the schema where documentation and editors can see it. §11.1's `set` and `set_global` are the next two callers |
| A49 | A `ref<C>` resolves against declared ids, and the id namespaces are separate: objects for a class target, the form's own `unique_in` namespace for a form target (§6.2, §7.4, §13.2) | §6.2 promised "validated at compile time" and nothing validated it, so any identifier satisfied any reference. Ten rules in the reference corpus were bound to actions nobody declared and could never have fired. Keeping the two namespaces apart is what stops an object called `lever` from satisfying a `ref<action>` that meant the verb |
| A17 | Flags are sugar over declared `bool` globals, not a separate store (§6.4.1) | As undeclared strings they are a silent-typo generator, which is exactly what the schema layer exists to prevent |
| A18 | Object-local `prop_def` still requires a declaration (§8.7) | One line buys typo detection, a type, an editor widget and a stable save key; the alternative reintroduces untyped looseness |
| A19 | Property access is statically checked with narrowing, plus an explicit runtime escape (§8.8.3) | Runtime-only moves authoring errors into play; static-only cannot reach scripts or honest subclass-varying cases |
| A20 | An absent property raises rather than defaulting (§8.8.4) | A `0` that should have been an error yields a game that is subtly wrong, which is far harder to find than one that is obviously broken |
| A31 | Core's requirements are declared as `core_requirement`, not implemented in silence (§7.2.5) | A rule enforced by code nobody can read is the ADRIFT/Inform wart wearing a different hat. A declared requirement fails by name |
| A35 | The requirement form is core-only and stays so, and is **rejected** when anything else declares one (§7.2.5.1) | A library's dependencies are checked by being used; core's live in C++, which the schema layer cannot see. A library appearing to need this would be evidence it had C++ in it, which §2.2 forbids — and a library that could assert requirements would be conscripting the engine's authority over other people's data |
| A32 | No declaration may be duplicated; `@replaces(lib)` is the deliberate form (§7.6) | Naming the source turns a typo or an upstream rename into a build failure, rather than a new declaration that silently never takes effect |
| A33 | `@replaces` rather than reusing `@override` (§7.6) | `@override` combines a value within a key; this supersedes a whole declaration. One word for two operations would hide the difference |
| A34 | `schema_extension` mirrors `class_extension`; `provides_schema` demoted to a checked manifest (§7.5, §13.3) | The spec previously pointed at a manifest field as though it were a mechanism, which it never was |
| A39 | Suggestions are deterministic by declaration order, and withheld when nothing is near (§7.3) | A "did you mean" is unusually prone to depending on iteration order, which §14.1 forbids and which nobody notices until two machines disagree. Withholding matters for the opposite reason: a confident wrong suggestion costs more trust than a missing one costs convenience |
| A29 | `includes` overloaded on `value` / `key` rather than a separate `has_key` (§6.5.1) | One question over two domains, with the argument name saying which. Acceptable because `exclusive_group` was already required by `rule`, `list_remove` and `present_in` — this makes an unmet need visible rather than inventing one |
| A30 | `exclusive_group` added to the schema layer (§7.2.1) | Three forms already documented mutually exclusive arguments in prose with no way to enforce them |
| A27 | Map keys are path segments — `location.exits.north` (§6.6.1) | Reads far better than the reader form for the overwhelmingly common literal-key case, and the compiler distinguishes key from property by the preceding segment's declared type. `map_get` remains for computed and non-identifier keys |
| A28 | A missing map key yields `none`; a missing property raises (§6.6.2) | Property existence is a static question about the type, so absence is a defect. Key presence is a runtime question about contents, and a room with no north exit is not a defect |
| A25 | Broad grammar tokens by default; narrowing comes from restrictions and rules, not from `[class:…]` (§8.8.1) | A narrow token makes a non-match fail *in the parser*, so the player is told they cannot see a laptop that is plainly there. Recovering the sensible refusal would mean a duplicate action per verb |
| A26 | Narrowing flows forward through pipeline stages, including from `restrictions` into `effects` (§8.8.3) | Without it, A25 would cost the author their static knowledge; with it, one restriction buys both the message and the type |
| A23 | A bare identifier in an argument position is always a global; object properties use a dotted path (§6.6.3) | Scope-sensitive arguments would mean the same identifier denotes a property in one block and a global in another, and moving a condition between blocks would silently change its meaning |
| A24 | `includes` rather than `contains` for collection membership (§6.5) | `containing` already means physical containment; two predicates one letter apart with unrelated meanings is a trap |
| A22 | `compare` as the single home for computed values (§10.6) | `count_of = { … } >= 2` is not merely unusual but ungrammatical — a statement is `Key Op Value`, so a dangling second operator has nowhere to live. One shallow form beats scattering value-producing predicates that cannot express their own result |
| A21 | Replication is specified but unscheduled; its parser constraint is not (§8.9) | Indistinguishable candidates arise from hand-declared objects too, so the parser needs the rule regardless |
| A16 | Naming builtins `the` / `a` / `name` rather than `theName` / `aName` / `PrintName` | The original notes used the longer forms; they read poorly under juxtaposition, and nothing has shipped, so the rename is free |

## Appendix B — Change log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 2026-08-11 | Initial specification, derived from `docs/proposal.md` v0.3 |

## Appendix C — Standard top-level forms

Ownership is three-way, per §7.2.4, and the **Owner** column below says which:

- **`format`** — a *format form*: parsed by `libs/stardata` with a reader of its own, declared in `libs/stardata/builtin/format.star`, sealed. What the data *means* may still be the engine's.
- **`starcore`** — core-owned: validated generically like any other form, and core's because core's own code reads the data. Declared in `libs/starcore/builtin/`, sealed.
- **`stdlib`** — ordinary Stardata with no privileged status, replaceable wholesale by a different library.

Libraries add more by declaring schemas (§7.2); this list is not closed.

| Form | Purpose | Owner | Specified in |
|---|---|---|---|
| `project` | Project manifest; exactly one per project | `starcore` | §13.1 |
| `library` | Library metadata, dependencies, editor features | `format` | §13.3 |
| `schema` | Declares a form or nested block shape | `format` | §7.2 |
| `schema_extension` | Adds keys to an existing form, including a sealed one | `format` | §7.5 |
| `core_requirement` | Asserts something `starcore` depends on | `format` | §7.2.5 |
| `enum` | Declares an enumerated value set | `format` | §6.2 |
| `style` | Declares a semantic text style | `starcore` | §9.3 |
| `global` | A mutable, saved, typed variable not owned by any object | `format` | §6.4 |
| `const` | An immutable, unsaved named value | `format` | §6.4 |
| `calendar` | Units and epoch for displaying and compiling times | `stdlib` | §11.6 |
| `loc` | Localisation table for one language | `starcore` | §9.6 |
| `class` | Declares a class | `format` | §8.1 |
| `class_extension` | Adds properties or changes defaults on an existing class | `format` | §8.2 |
| `backdrop` | An object present in several rooms at once | `stdlib` | §8.6 |
| `door` | A two-sided object joining two rooms | `stdlib` | §8.6.1 |
| `trait` | Declares an orthogonal capability bundle | `format` | §8.3 |
| *class name* | Instantiates an object of that class | — | §7.4 |
| `sector` | Declares a loading, simulation and pinning unit | `starcore` | §11.4 |
| `action` | Declares a player- or NPC-performable action | `starcore` | §10, §11 |
| `rule` | Modifies an action, or reacts to an event | `starcore` | below |
| `turn_hook` | Registers a ruleset hook at a named turn phase | `starcore` | below |
| `quest` | Declares a quest, its stages and completion predicates | `stdlib` | §10, §11 |
| `dialogue` | Declares a conversation graph | `stdlib` | below |
| `goal_def` | Declares a multi-round NPC plan | `stdlib` | below |
| `schedule` | Declares an NPC's time-of-day routine | `stdlib` | §11.6 |
| `bark_table` | Weighted one-line NPC utterances | `stdlib` | below |
| `lexicon` | Word forms overriding the language pack's inferences | `stdlib` | §9.5.2 |
| `party` | Declares a party, its members and control model | `stdlib` | §11.7 |

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
