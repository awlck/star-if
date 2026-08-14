# EXPECT E-UNICODE-WS
# spec §3.1 — only tab, LF, CR and space separate tokens. Other Unicode
# whitespace is rejected with a suggestion rather than silently accepted,
# because it arrives by copy-and-paste and is invisible in every editor.
# The space before the "=" below is U+00A0, and the one after "id" is U+2007.
room = {
    id = pasted_from_a_web_page
    short_name = "Cargo Bay"
}
