// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/schema/schema.hpp"

namespace stardata::schema {

class SchemaSet;

// The mechanism half of backlog F12: §8.8.2's three static answers, and the
// class walk they are computed from.
//
// WHY THIS IS HERE AND THE ANALYSIS IS NOT. F12 is marked "owner: split" and
// this is where the split falls. Deciding whether a class declares a property
// is a question about the class graph, which is `libs/stardata`'s to answer;
// deciding that `noun` is a slot, that `of_class` narrows it and that
// `has_prop` escapes is interactive-fiction vocabulary (§10.2, §10.4), which
// is `libs/starcore`'s. So the classifier lives here, names nothing, and the
// pass that drives it lives beside the placement pass.
//
// The same line proposal §2.1.1 draws, and the same one F2c drew for
// placement -- with the difference that here it was drawn before the pass was
// written rather than after it had leaked.

// §8.8.2, exactly.
enum class PropertyAnswer : std::uint8_t {
    // "`T` or an ancestor or trait of `T` declares `P`. Compile to a direct
    // access."
    Present,
    // "Some objects satisfying `T` declare `P` and others do not. The author
    // must resolve it."
    Maybe,
    // "Nothing in the program that could satisfy `T` declares `P`. **Error.**
    // This is the case that catches typos, and it is the common one."
    Absent,
};

[[nodiscard]] std::string_view to_string(PropertyAnswer answer) noexcept;

struct PropertyLookup {
    PropertyAnswer answer = PropertyAnswer::Absent;

    // The declaration, when the answer is Present. Null otherwise.
    const PropDecl* declaration = nullptr;

    // The classes and traits that do declare `P`, in declaration order.
    //
    // For Maybe these are the descendants of `T` that declare it, which is
    // what §8.8.3 asks the diagnostic to name. For Absent they are every
    // declarer in the program, because an Absent read is usually a typo and
    // the useful thing to say is where the property actually lives.
    std::vector<std::string> declared_by;
};

// §8.8.2's classification for a read of `name` on a slot of static type
// `type`.
//
// OBJECT-LOCAL DECLARATIONS COUNT, and leaving them out would have made this
// actively harmful. §8.7 lets one object declare a property nothing else has;
// a slot typed `thing` reading it is "possibly present", not absent. A walk
// over classes alone would call it Absent and reject correct code -- so the
// registry tracks the property names object instantiations declare, and F11
// is what put them there.
[[nodiscard]] PropertyLookup classify_property(std::string_view name, const ClassDecl& type,
                                               const SchemaSet& set);

// Whether `candidate` is `ancestor`, or descends from it, following
// `of_class`. Traits are not a hierarchy (§8.3) and never satisfy this.
[[nodiscard]] bool descends_from(const ClassDecl& candidate, std::string_view ancestor,
                                 const SchemaSet& set);

// Every property name reachable on `type` -- its own, its ancestors', and
// the traits of each. In resolution order (§8.4), so the first of a repeated
// name is the one that wins.
//
// Exposed because a "did you mean" over the properties a slot really has is
// the useful suggestion for an Absent read (backlog F6), and the caller that
// wants it is in the other library.
[[nodiscard]] std::vector<std::string_view> reachable_properties(const ClassDecl& type,
                                                                 const SchemaSet& set);

// `type`, then each ancestor, in §8.4's resolution order, following `of_class`
// and then §8.1.1's declared root.
//
// THE ONLY INHERITANCE WALK IN THIS LIBRARY, which it had to be told to be.
// `schema/types.cpp` grew a second one for F4's instantiation type check,
// eighteen months of tasks before this one existed, and the two disagreed:
// that walk ignored traits and stopped at a parentless class, so a value
// written for a property arriving through a trait -- `lumens = "very"` on a
// class mixing in a trait that declares `lumens = int` -- was accepted in
// silence. Two walks over one graph will always eventually answer differently;
// the fix was to delete one, not to teach both.
[[nodiscard]] std::vector<const ClassDecl*> lineage(const ClassDecl& type, const SchemaSet& set);

} // namespace stardata::schema
