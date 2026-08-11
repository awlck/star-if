# EXPECT E-BLOCK-MIXED
# spec §8.7 — an object-local prop_def may not redeclare an inherited name
# with a different type. Detecting that needs the schema layer; this fixture
# pins the syntax and fails today on a shape error.
class = {
    id       = console
    of_class = thing
    prop_def = { times_rebooted = int }
}

thing = {
    id       = odd_console
    of_class = console
    prop_def = { times_rebooted = string  diagnostic_code }
}
