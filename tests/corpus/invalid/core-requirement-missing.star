# EXPECT E-CORE-REQUIREMENT
# spec §7.2.2 — "the absence of anything core requires" is reported at load,
# naming the requirement, rather than surfacing much later as a failure
# nobody can account for. Nothing declares `starcore.hovercraft`.
core_requirement = {
    id       = hovercraft_exists
    requires = class
    subject  = starcore.hovercraft
    doc      = "A requirement nothing satisfies, so that the check itself is tested."
}
