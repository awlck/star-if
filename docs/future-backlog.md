# Beyond Phase 0 — open design points

**Companion to:** `docs/phase-0-backlog.md`

Phase 0 work keeps finding design questions that are real but not Phase 0's
to answer — either because the exit criterion doesn't need them settled, or
because answering them well needs machinery (the semantic analyzer, the
editor, the runtime) that doesn't exist yet. `phase-0-backlog.md`'s own
`[OPEN]` markers used to hold these, which worked until they stopped being
about Phase 0 at all and started being proposals for later phases with their
own shape. This file is where those go instead, so the Phase 0 backlog stays
about Phase 0 and a design thought from week three doesn't get lost between
here and whichever phase eventually picks it up.

Entries are dated by the session that raised them, not sized or sequenced —
none of this is scheduled. When a phase that could act on an entry starts,
it moves into that phase's own backlog and is deleted from here.

---

## Action grammar, edited without copying the whole action

**Raised:** alongside backlog F2d, while giving `tour.star`'s `take` and
`open` `@replaces(stdlib)` overrides for their grammar.

`@replaces` supersedes a whole declaration, and for `action` that means all
of it — `match`, `verb`, every rule, every message — even when only the
grammar lines are what an author actually wants to change. There is no
`action_extension` the way there is a `class_extension`, so widening or
narrowing an action's vocabulary today means copying the entire declaration
and marking the copy `@replaces(lib)`, which is exactly what `tour.star`'s
`take` and `open` do.

A graphical editor could make this less costly without a new language
mechanism: transparently copy a library action's declaration the first time
an author touches its `match`, which is the same thing they would otherwise
do by hand. Whether a later library update should still reach that action
once it has been copied is a real question either answer to which has a
cost — a copy that stops tracking its original, or an editor-made copy that
silently drifts from what the library ships. Not designed here.

---

## Rule identity and replacement

**Raised:** alongside backlog F2d, then extended in a follow-up design pass.

### The problem

`rule` has no `unique_in` (spec §7.2), which is what exempts several rules
on one action or trait from counting as duplicates of each other — and the
same absence means a rule a library declares has no id a later declaration
could name to replace or remove specifically.

A rule attaches to an action (or an event) by `of_action` / `of_event`,
never to the trait or action it happens to be *written inside* — that is
only a convenient place to declare one, and the compile-time dispatch index
(proposal §7.3) collects every rule in the loaded program by the action or
event it names, regardless of which declaration nested it. Which makes
under-replacing worse than it first looks: superseding an action's own
declaration with `@replaces` only discards the rules nested directly inside
*that* declaration. A rule contributed by some *other* declaration (a trait,
say) that names the same action by id is untouched — it keeps firing against
whatever the action now is, since nothing about `@replaces` knows the two
are related beyond the id string. There was no way to reach into that other
declaration and take out, or alter, the one rule that no longer belongs.

### Proposed semantics

1. `rule` gains an optional `id`. Where given, it must be unique (the usual
   `unique_in` machinery). A rule with no `id` either has none, or gets one
   generated, whichever turns out more straightforward to implement.
2. A rule with no `id` warns **in library code**, not in game/project code —
   a project's own one-off rules don't need to be individually addressable,
   but a library's do, since anything else might want to replace one later.
3. **Replacing the action or trait a rule is nested inside throws that rule
   out along with it.** To be documented explicitly in the spec (§7.6),
   since it is exactly the surprising direction the problem above describes
   — the *other* direction (a rule surviving its container's replacement)
   is the one that reads as obviously wrong and is what this whole entry is
   about avoiding.
4. A rule declared nested inside an action or trait can still be replaced
   *individually*, with a top-level `@replaces` statement naming its `id`,
   as if that rule had been written at the top level all along. (Whether a
   standalone top-level `rule = { id = … }` declaration is legal on its own,
   or only ever appears when replacing a nested one, is not settled here.)
5. **Later, once the semantic analyzer can do this kind of check:** a rule
   declared nested inside an action or trait but whose own condition/effect
   language never actually touches that action or trait gets a warning,
   with a suggestion to move it to the top level — the nesting is supposed
   to mean something (§8.3's "attach to whatever mixes the trait in", or
   simply "this is about that action"), and a rule that doesn't honor it is
   probably misplaced or copy-pasted from somewhere else.

Point 5 depends on static analysis Phase 0 doesn't have yet (walking a
rule's own condition/effect blocks for references to its container), so it
is explicitly the furthest-out part of this entry — everything else here is
closer to reach once `libs/stardata`/`libs/starcore` support per-declaration
ids for a non-`unique_in`-having form at all.

---

## A verify-phase-style elimination mechanism for actions

**Raised:** while removing `action`'s `conditions` stage (see
`phase-0-backlog.md`, F12).

`conditions` was removed from `action`'s own schema because it was silent
by §10.5 with no fallback: it could eliminate the whole action from
consideration with nothing shown to the player, which is a worse silence
than a `rule`'s `conditions` gating out one response (another rule, or the
action's default behaviour, can still answer). `restrictions` is the right
place for a check like this today, and `stdlib`'s `open`/`close` were moved
there accordingly.

A later phase could still want something conditions-*shaped* at the action
level — a TADS3/adv3-style `verify` phase that runs during disambiguation,
deciding which of several matching objects an action even applies to. That
is a dispatch mechanism, not a silent gate, and it still owes the player a
message when it eliminates the last remaining candidate. Nothing has
designed that mechanism; this entry only records that removing `conditions`
was not an attempt to design it, and that whatever eventually fills that
role needs to solve the exact silence problem `conditions` didn't.
