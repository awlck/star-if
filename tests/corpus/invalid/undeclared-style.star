# EXPECT E-STYLE-UNDECLARED
# spec §9.3 — styles are semantic and must be declared, so that a theme can
# map them and a monochrome frontend can degrade them.
style = { id = known_style }

weapon = {
    id      = mystery
    tooltip = @style(never_declared) "[Name(self)]"
}
