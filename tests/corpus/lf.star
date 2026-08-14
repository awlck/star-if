# =============================================================================
#  lf.star — line-ending round-trip fixture (spec §2): LF endings
# =============================================================================
#
#  Deliberately checked in with LF (Unix) line endings, byte for byte. Spec §2
#  requires that CRLF and LF both survive UNCHANGED through the format — an
#  implementation must not normalise them. .gitattributes marks *.star as
#  -text specifically so Git never rewrites this file on checkout or
#  checkin, on any platform.
#
#  See crlf.star for the CRLF twin of this file (same content, different
#  line ending), and CONTRIBUTING.md ("Line endings") for the full story.
#
#  Byte-for-byte round-trip through the real parser is backlog task E6; this
#  file's job today is just to exist as an LF fixture that Git does not
#  corrupt, and to keep validating under tests/check_stardata.py.
# =============================================================================

room = {
    id     = fixture_room
    sector = fixture_sector
    name   = $room_fixture
    description = "A small room that exists only to prove a line ending "
                  "survived the trip."
    exits  = { north = fixture_room_two }
}

room = {
    id     = fixture_room_two
    sector = fixture_sector
    name   = $room_fixture_two
    exits  = { south = fixture_room }
}

loc = {
    lang = en

    room_fixture     = "Fixture Room"
    room_fixture_two = "Second Fixture Room"
}
