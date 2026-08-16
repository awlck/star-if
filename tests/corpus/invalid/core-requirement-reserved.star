# EXPECT E-CORE-RESERVED
# spec §7.2.5.1 — `core_requirement` is a reserved internal form. Core needs
# it because its C++ reads the world store directly and the schema layer
# cannot see C++; a library has no such gap, since what a library depends on
# is checked by being used. A library that could assert requirements would be
# asserting them about other people's data, at load, in core's voice.
core_requirement = {
    id       = my_library_wants_this
    requires = class
    subject  = weapon
    doc      = "A library overstepping, which is what this fixture is for."
}
