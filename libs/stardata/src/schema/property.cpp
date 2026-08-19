// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/property.hpp"

#include <algorithm>
#include <cassert>

#include "stardata/schema/loader.hpp"

namespace stardata::schema {

namespace {

// A depth cap rather than a visited set, matching `find_declared_property`:
// a cycle in `of_class` is a malformed hierarchy, which is the class graph's
// to report and not this walk's to diagnose on the way past.
constexpr int kMaxDepth = 64;

// `type`, then each ancestor, in resolution order (§8.4 step 3).
[[nodiscard]] std::vector<const ClassDecl*> lineage(const ClassDecl& type, const SchemaSet& set,
                                                    std::string_view implicit_parent) {
    std::vector<const ClassDecl*> chain;
    const ClassDecl* at = &type;
    for (int depth = 0; at != nullptr && depth < kMaxDepth; ++depth) {
        chain.push_back(at);
        if (!at->of_class.empty()) {
            at = set.find_class(at->of_class);
            continue;
        }
        // No `of_class`. Whether that means "root" or "child of the object
        // model's root" is the caller's to say -- see the header.
        if (implicit_parent.empty() || at->id == implicit_parent || at->is_trait) {
            break;
        }
        at = set.find_class(implicit_parent);
    }
    return chain;
}

// Whether `decl` or one of its traits declares `name`.
[[nodiscard]] const PropDecl*
declared_here_or_by_trait(const ClassDecl& decl, std::string_view name, const SchemaSet& set) {
    if (const PropDecl* own = decl.find_property(name)) {
        return own;
    }
    // §8.4 resolves traits in declaration order, so the first that declares
    // it wins -- which is also what §8.3's `resolve` exists to make explicit
    // when two of them collide.
    for (const std::string& id : decl.traits) {
        const ClassDecl* trait = set.find_trait(id);
        if (trait == nullptr) {
            continue; // an undeclared trait; not this walk's to report
        }
        if (const PropDecl* from_trait = trait->find_property(name)) {
            return from_trait;
        }
    }
    return nullptr;
}

} // namespace

std::string_view to_string(PropertyAnswer answer) noexcept {
    switch (answer) {
    case PropertyAnswer::Present:
        return "present";
    case PropertyAnswer::Maybe:
        return "possibly present";
    case PropertyAnswer::Absent:
        return "absent";
    }
    assert(false && "unhandled PropertyAnswer");
    return "absent";
}

bool descends_from(const ClassDecl& candidate, std::string_view ancestor, const SchemaSet& set,
                   std::string_view implicit_parent) {
    if (candidate.is_trait) {
        return false; // §8.3: a trait is mixed in, not inherited from
    }
    for (const ClassDecl* at : lineage(candidate, set, implicit_parent)) {
        if (at->id == ancestor) {
            return true;
        }
    }
    return false;
}

std::vector<std::string_view> reachable_properties(const ClassDecl& type, const SchemaSet& set,
                                                   std::string_view implicit_parent) {
    std::vector<std::string_view> names;
    const auto add = [&names](const std::vector<PropDecl>& properties) {
        for (const PropDecl& property : properties) {
            if (std::find(names.begin(), names.end(), property.name) == names.end()) {
                names.emplace_back(property.name);
            }
        }
    };

    for (const ClassDecl* at : lineage(type, set, implicit_parent)) {
        add(at->properties);
        for (const std::string& id : at->traits) {
            if (const ClassDecl* trait = set.find_trait(id)) {
                add(trait->properties);
            }
        }
    }
    return names;
}

PropertyLookup classify_property(std::string_view name, const ClassDecl& type, const SchemaSet& set,
                                 std::string_view implicit_parent) {
    PropertyLookup result;

    // Present: `T` or an ancestor or trait of `T` declares it (§8.8.2).
    for (const ClassDecl* at : lineage(type, set, implicit_parent)) {
        if (const PropDecl* declared = declared_here_or_by_trait(*at, name, set)) {
            result.answer = PropertyAnswer::Present;
            result.declaration = declared;
            result.declared_by.push_back(at->id);
            return result;
        }
    }

    // Maybe: something that could satisfy `T` declares it. "Could satisfy"
    // means a descendant class -- an object of a subclass is an object of the
    // class -- and, since §8.7 exists, an object that declared it for itself.
    std::vector<std::string> descendants;
    for (const ClassDecl& candidate : set.classes()) {
        if (candidate.is_trait || candidate.id == type.id) {
            continue;
        }
        if (!descends_from(candidate, type.id, set, implicit_parent)) {
            continue;
        }
        if (declared_here_or_by_trait(candidate, name, set) != nullptr) {
            descendants.push_back(candidate.id);
        }
    }
    for (const SchemaSet::LocalProperty& local : set.local_properties()) {
        if (local.name != name) {
            continue;
        }
        const ClassDecl* of = set.find_class(local.class_id);
        if (of != nullptr &&
            (of->id == type.id || descends_from(*of, type.id, set, implicit_parent)) &&
            std::find(descendants.begin(), descendants.end(), local.class_id) ==
                descendants.end()) {
            descendants.push_back(local.class_id);
        }
    }
    if (!descendants.empty()) {
        result.answer = PropertyAnswer::Maybe;
        result.declared_by = std::move(descendants);
        return result;
    }

    // Absent. The useful thing to say is where the property does live, since
    // this is overwhelmingly a typo -- so the list is every declarer in the
    // program rather than the empty set of relevant ones.
    result.answer = PropertyAnswer::Absent;
    for (const ClassDecl& candidate : set.classes()) {
        if (candidate.find_property(name) != nullptr) {
            result.declared_by.push_back(candidate.id);
        }
    }
    return result;
}

} // namespace stardata::schema
