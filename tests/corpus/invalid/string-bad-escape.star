# EXPECT E-STR-ESCAPE
# spec §3.5 — the escape set is closed: \" \\ \n \t \[ \] \$ \@ and \uXXXX.
# Anything else is rejected rather than passed through, so that adding an
# escape later cannot change the meaning of text already written.
loc = {
    lang = en
    bad_escape  = "A path written the Windows way: C:\Users\vex"
    bad_unicode = "A surrogate half is not a character: \ud800"
}
