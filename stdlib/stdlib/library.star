# =============================================================================
#  stdlib — the standard library's own manifest
# =============================================================================
#
#  Everything in this directory is ordinary Stardata with NO privileged status
#  (spec §7.2.4, last paragraph). `thing`, `person`, `container`, every action
#  and every message could be replaced wholesale by a different library, and
#  a test asserts that this directory uses only mechanisms available to any
#  library — it declares no core-owned form, seals nothing, and names nothing
#  in the `starcore.` namespace.
#
#  That is the counterpart to libs/starcore/builtin/ being sealed: core owns
#  the small set of things it reads and writes itself, and nothing else. The
#  boundary is only meaningful if the library side of it is genuinely
#  unprivileged, so it is worth testing rather than asserting in prose.
# =============================================================================

library = {
    id           = stdlib
    version      = "0.1.0"
    display_name = "STAR IF core library"
    doc          = "Kinds, actions, parser grammar and default messages."
}
