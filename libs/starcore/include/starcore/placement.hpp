// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/loader.hpp"

namespace starcore {

// Placement (spec §8.5, backlog F2c) -- and the first C++ in `libs/starcore`.
//
// WHY THIS IS HERE AND NOT IN `stardata`. Proposal §2.1.1 draws the line at
// mechanism versus vocabulary: `stardata` knows that forms have keys, that
// keys have types and arities, that a schema can be sealed and extended and
// replaced. It does not know what any form *means*. `starcore` knows what
// `holder` and `relation` are.
//
// Placement is vocabulary through and through. `in = ornate_box` is sugar for
// two specific slots of one specific class, and there is no way to state the
// rule without naming them. It lived in `stardata` for one release because
// that is where the schema layer was being written, and passing the relation
// keywords in as a `vector<string>` made it *look* generic -- but a mechanism
// with exactly one possible user is not a mechanism, it is a semantic pass
// wearing a parameter. So the parameter is gone: the code below names
// `relation_enum` outright, because here that is the right thing to do.
//
// This is also why the file does not try to generalise. §2.1.1 makes the
// point directly: parameterising the *stage order* rescues F12's narrowing,
// because the dataflow really is generic; doing the same to placement would
// produce a mechanism nobody else could use.

// The enum whose values are §8.5's placement keywords, declared in
// `libs/starcore/builtin/object.star` as the type of `starcore.object`'s
// `relation` property.
//
// Named here as a plain constant, which is the whole difference the move
// makes. Inside `stardata` this name had to arrive through a setter, so that
// the format library never learned that placement existed; inside `starcore`
// it is simply core's name for core's own vocabulary.
inline constexpr std::string_view relation_enum_id = "relation_enum";

// Where an object starts out.
//
// Every object has at most one parent, and the parent link carries a
// relation. `in = ornate_box` is sugar for `holder = ornate_box  relation =
// in`, and both spellings produce identical data -- the sugar exists because
// placement is written for nearly every object in a game, the long form
// because it is what the two slots actually are and because a computed
// placement has no keyword to use.
//
// THE SUGAR IS EXPANDED HERE AND NEVER IN THE TREE. §14.2 requires that a
// round-trip reproduce what the author wrote, so `in = box` has to still say
// `in = box` after a parse and a write. This is the semantic view; the CST
// keeps the spelling.
struct Placement {
    std::string holder;   // the id of the containing object
    std::string relation; // one of the values of the relation enum
    bool from_sugar = false;
    stardata::diag::Span span; // the key that established it
};

// The relation keywords, read out of the enum rather than listed in code.
//
// Still data, and still worth insisting on: a ruleset that supersedes
// `relation_enum` with `@replaces(starcore)` (§7.6) gets its own keywords
// working as sugar with no change here. Owning a vocabulary is not the same
// as hard-coding one.
[[nodiscard]] const std::vector<std::string>&
relation_keywords(const stardata::schema::SchemaSet& set);

// The placement an object instantiation block declares, if any.
//
// Reports when a block writes both spellings: they are the same two slots,
// and §8.5 says the conflict is not resolvable by precedence -- so neither
// wins, and nothing is returned.
[[nodiscard]] std::optional<Placement> read_placement(const stardata::ast::Block& block,
                                                      const stardata::schema::SchemaSet& set,
                                                      stardata::diag::DiagnosticSink& sink);

// Every object instantiation in a parsed file, checked for §8.5's conflict.
//
// A separate pass over the tree rather than a hook inside `load_directory`,
// because that is what the boundary means in practice: `stardata` loads and
// validates, then whoever owns the vocabulary walks the result. `starforge`
// will sequence the two in Phase 1; the tests sequence them today.
void check_placements(const stardata::ast::File& file, const stardata::schema::SchemaSet& set,
                      stardata::diag::DiagnosticSink& sink);

} // namespace starcore
