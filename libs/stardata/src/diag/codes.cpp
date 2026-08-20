// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/diag/codes.hpp"

#include <cassert>

namespace stardata::diag {

std::string_view to_string(Severity severity) noexcept {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    assert(false && "unhandled Severity");
    return "error";
}

std::string_view code_string(Code code) noexcept {
    switch (code) {
    case Code::StringMultiline:
        return "E-STR-MULTILINE";
    case Code::StringEscape:
        return "E-STR-ESCAPE";
    case Code::StringUnterminated:
        return "E-STR-UNTERMINATED";
    case Code::DecimalPrecision:
        return "E-DEC-PRECISION";
    case Code::DecimalLeadingDot:
        return "E-DEC-LEADING-DOT";
    case Code::NumberTrailingDot:
        return "E-NUM-TRAILING-DOT";
    case Code::IntegerRange:
        return "E-INT-RANGE";
    case Code::BracketOutside:
        return "E-BRACKET-OUTSIDE";
    case Code::UnicodeWhitespace:
        return "E-UNICODE-WS";
    case Code::BadChar:
        return "E-BAD-CHAR";
    case Code::Utf8Invalid:
        return "E-UTF8-INVALID";
    case Code::OpRemoved:
        return "E-OP-REMOVED";
    case Code::BraceUnbalanced:
        return "E-BRACE-UNBALANCED";
    case Code::StrayToken:
        return "E-STRAY-TOKEN";
    case Code::ValueMissing:
        return "E-VALUE-MISSING";
    case Code::SchemaInvalid:
        return "E-SCHEMA-INVALID";
    case Code::SchemaDuplicate:
        return "E-SCHEMA-DUPLICATE";
    case Code::SchemaSealed:
        return "E-SCHEMA-SEALED";
    case Code::KeyMissing:
        return "E-KEY-MISSING";
    case Code::CoreReparent:
        return "E-CORE-REPARENT";
    case Code::CoreRequirement:
        return "E-CORE-REQUIREMENT";
    case Code::CoreReserved:
        return "E-CORE-RESERVED";
    case Code::ProvidesMismatch:
        return "W-PROVIDES-MISMATCH";
    case Code::PlacementConflict:
        return "E-PLACEMENT-CONFLICT";
    case Code::ExclusiveGroup:
        return "E-EXCLUSIVE-GROUP";
    case Code::ExclusiveMissing:
        return "E-EXCLUSIVE-MISSING";
    case Code::BlockMixed:
        return "E-BLOCK-MIXED";
    case Code::DuplicateKey:
        return "E-DUP-KEY";
    case Code::UnknownKey:
        return "E-UNKNOWN-KEY";
    case Code::UnknownAnnotation:
        return "E-UNKNOWN-ANNOTATION";
    case Code::AnnotationConflict:
        return "E-ANNOT-CONFLICT";
    case Code::AnnotationMisapplied:
        return "E-ANNOT-MISAPPLIED";
    case Code::AnnotationArgument:
        return "E-ANNOT-ARGUMENT";
    case Code::EqInBinding:
        return "E-EQ-IN-BINDING";
    case Code::AssignInCondition:
        return "W-ASSIGN-IN-COND";
    case Code::ReservedWord:
        return "E-RESERVED-WORD";
    case Code::TypeMismatch:
        return "E-TYPE-MISMATCH";
    case Code::RefUnresolved:
        return "E-REF-UNRESOLVED";
    case Code::TraitConflict:
        return "E-TRAIT-CONFLICT";
    case Code::FlagUndeclared:
        return "E-FLAG-UNDECLARED";
    case Code::FlagNotBool:
        return "E-FLAG-NOT-BOOL";
    case Code::GlobalUndeclared:
        return "E-GLOBAL-UNDECLARED";
    case Code::DatumUnresolved:
        return "E-DATUM-UNRESOLVED";
    case Code::DatumAmbiguous:
        return "E-DATUM-AMBIGUOUS";
    case Code::PathNotRef:
        return "E-PATH-NOT-REF";
    case Code::PropAbsent:
        return "E-PROP-ABSENT";
    case Code::PropMaybeAbsent:
        return "E-PROP-MAYBE-ABSENT";
    case Code::PropDefTypeMismatch:
        return "E-PROPDEF-TYPE-MISMATCH";
    case Code::PropDefRedundant:
        return "W-PROPDEF-REDUNDANT";
    case Code::NamesSubset:
        return "W-NAMES-SUBSET";
    case Code::GlobalUnused:
        return "W-GLOBAL-UNUSED";
    case Code::ContainmentCycle:
        return "E-CONTAINMENT-CYCLE";
    case Code::FailmsgSilent:
        return "E-FAILMSG-SILENT";
    case Code::FailmsgUnreachable:
        return "E-FAILMSG-UNREACHABLE";
    case Code::FailmsgMissing:
        return "W-FAILMSG-MISSING";
    case Code::TryActionCycle:
        return "E-TRYACTION-CYCLE";
    case Code::TemplateBrackets:
        return "E-TEMPLATE-BRACKETS";
    case Code::StyleUndeclared:
        return "E-STYLE-UNDECLARED";
    case Code::LocDuplicate:
        return "E-LOC-DUPLICATE";
    case Code::LocUndefined:
        return "E-LOC-UNDEFINED";
    case Code::LocUnused:
        return "W-LOC-UNUSED";
    case Code::RemoveAbsent:
        return "W-REMOVE-ABSENT";
    case Code::ClassExtensionConflict:
        return "I-CLASS-EXT-CONFLICT";
    case Code::TraitCountHigh:
        return "I-TRAIT-COUNT";
    }
    assert(false && "unhandled Code");
    return "E-UNKNOWN";
}

Severity default_severity(Code code) noexcept {
    switch (code) {
    case Code::AssignInCondition:
    case Code::ProvidesMismatch:
    case Code::PropDefRedundant:
    case Code::NamesSubset:
    case Code::GlobalUnused:
    case Code::FailmsgMissing:
    case Code::RemoveAbsent:
    case Code::LocUnused:
        return Severity::Warning;
    case Code::ClassExtensionConflict:
    case Code::TraitCountHigh:
        return Severity::Info;
    default:
        return Severity::Error;
    }
}

} // namespace stardata::diag
