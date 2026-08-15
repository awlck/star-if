// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <string_view>

namespace stardata::diag {

enum class Severity : std::uint8_t { Info, Warning, Error };

[[nodiscard]] std::string_view to_string(Severity severity) noexcept;

// One entry per row of the required-diagnostics table, spec §14.3
// ("Diagnostics"; the backlog's own reference to "§15" is to a section that
// was renumbered to "Reserved for future use" -- §14.3 is the table that
// actually exists), preceded by the lexical conditions of §3, which that
// table does not cover. The string codes are stable identifiers: CI assertions
// and the `# EXPECT` fixtures under tests/corpus/invalid/ key on them, not
// on message text, so a code must never be renamed once it ships.
//
// Reuses the strings already defined in tests/check_stardata.py's CODES
// table wherever that script and this table diagnose the same condition
// (backlog C2's "stable codes worth reusing"), so the Python checker and
// the C++ implementation share one vocabulary during the overlap period
// that backlog H2 describes.
enum class Code : std::uint8_t {
    // clang-format off

    // Lexical (spec §3). These predate §14.3's table, which begins at §5.2
    // and so lists none of them, but §3 states each as a MUST and
    // tests/check_stardata.py already reports all but the last two under
    // these names. Backlog D3 enumerates the set the lexer must produce.
    StringMultiline,        // E-STR-MULTILINE         §3.5    string literal spans a line terminator
    StringEscape,           // E-STR-ESCAPE            §3.5    invalid escape sequence
    StringUnterminated,     // E-STR-UNTERMINATED      §3.5    string literal not closed before EOF
    DecimalPrecision,       // E-DEC-PRECISION         §3.4    decimal without exactly three fractional digits
    DecimalLeadingDot,      // E-DEC-LEADING-DOT       §3.4    decimal written as '.5' rather than '0.5'
    NumberTrailingDot,      // E-NUM-TRAILING-DOT      §3.4    number written as '5.' rather than '5.0'
    IntegerRange,           // E-INT-RANGE             §3.4    integer literal outside signed 64-bit range
    BracketOutside,         // E-BRACKET-OUTSIDE       §3.7    '[' or ']' outside a string literal
    UnicodeWhitespace,      // E-UNICODE-WS            §3.1    non-ASCII whitespace
    BadChar,                // E-BAD-CHAR              §3      character that begins no token
    Utf8Invalid,            // E-UTF8-INVALID          §2      file is not well-formed UTF-8

    // Syntactic (spec §4, §5.2). E-BRACE-UNBALANCED and E-STRAY-TOKEN reuse
    // the names tests/check_stardata.py already reports, so the two
    // implementations keep one vocabulary through the overlap of backlog H2.
    BraceUnbalanced,        // E-BRACE-UNBALANCED      §4      a '{' with no '}', or a '}' with no '{'
    StrayToken,             // E-STRAY-TOKEN           §4      a token where the grammar allows none
    ValueMissing,           // E-VALUE-MISSING         §4      an operator with no value after it

    // Spec §14.3's required-diagnostics table, one entry per row.
    BlockMixed,             // E-BLOCK-MIXED           §5.2    mixed list/record contents in one block
    DuplicateKey,           // E-DUP-KEY               §5.3    duplicate key, arity = one; cites both spans
    UnknownKey,              // E-UNKNOWN-KEY           §7.3    unknown key in a closed schema
    UnknownAnnotation,        // E-UNKNOWN-ANNOTATION    §3.7    unknown annotation
    EqInBinding,              // E-EQ-IN-BINDING         §3.6    '==' used in a binding context
    AssignInCondition,        // W-ASSIGN-IN-COND        §3.6    bare '=' used in a condition context
    ReservedWord,             // E-RESERVED-WORD         §3.8    true/false used as a value
    TypeMismatch,             // E-TYPE-MISMATCH         §6.2    value does not match the declared type
    RefUnresolved,            // E-REF-UNRESOLVED        --      unresolvable ref<C>, or wrong class
    TraitConflict,            // E-TRAIT-CONFLICT        §8.3    trait property conflict without `resolve`
    FlagUndeclared,           // E-FLAG-UNDECLARED       §6.4.1  set_flag/clear_flag/flag_set names no bool global
    FlagNotBool,              // E-FLAG-NOT-BOOL         §6.4.1  ...names a global that is not of type bool
    GlobalUndeclared,         // E-GLOBAL-UNDECLARED     §6.4    reference to an undeclared global/const
    DatumUnresolved,          // E-DATUM-UNRESOLVED      §6.6    <datum> resolves neither as id nor as a path
    DatumAmbiguous,           // E-DATUM-AMBIGUOUS       §6.6.4  <datum> resolves both ways; names both candidates
    PathNotRef,               // E-PATH-NOT-REF          §6.6    a path segment's intermediate isn't ref<C>-typed
    PropAbsent,               // E-PROP-ABSENT           §8.8.2  property read is definitely absent
    PropMaybeAbsent,          // E-PROP-MAYBE-ABSENT     §8.8.3  property read is possibly absent, not narrowed
    PropDefTypeMismatch,      // E-PROPDEF-TYPE-MISMATCH §8.7    local prop_def redeclares with a different type
    PropDefRedundant,         // W-PROPDEF-REDUNDANT     §8.7    local prop_def redeclares with the same type
    NamesSubset,              // W-NAMES-SUBSET          proposal §6.4.1  object's names are a strict subset of another's
    GlobalUnused,             // W-GLOBAL-UNUSED         §6.4    declared global/const never read
    ContainmentCycle,         // E-CONTAINMENT-CYCLE     §8.5    containment cycle
    FailmsgSilent,            // E-FAILMSG-SILENT        §10.5   failureMsg in a silent context
    FailmsgUnreachable,       // E-FAILMSG-UNREACHABLE   §10.5.1 failureMsg below a NOT/OR/COUNT_AT_LEAST
    FailmsgMissing,           // W-FAILMSG-MISSING       §10.5.3 restriction with no reachable failureMsg
    TryActionCycle,           // E-TRYACTION-CYCLE       §11.2   statically provable try_action cycle
    StyleUndeclared,          // E-STYLE-UNDECLARED      §9.3    undeclared style name
    LocDuplicate,             // E-LOC-DUPLICATE         §9.6    duplicate localisation key
    RemoveAbsent,             // W-REMOVE-ABSENT         §6.3    '-=' removing an absent entry
    ClassExtensionConflict,   // I-CLASS-EXT-CONFLICT    §8.2    two class_extensions set the same default
    TraitCountHigh,           // I-TRAIT-COUNT           §8.3    project crosses 64 declared traits
    // clang-format on
};

// The stable string form, e.g. "E-DUP-KEY". Never changes once a code ships.
[[nodiscard]] std::string_view code_string(Code code) noexcept;

// The severity spec §14.3 assigns to this code. A caller may still construct
// a Diagnostic with a different severity (e.g. `--strict` promoting a
// warning to an error); this is only the default absent a reason otherwise.
[[nodiscard]] Severity default_severity(Code code) noexcept;

} // namespace stardata::diag
