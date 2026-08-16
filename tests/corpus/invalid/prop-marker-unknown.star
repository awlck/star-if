# EXPECT E-UNKNOWN-KEY
# spec §7.2.3 — the marker vocabulary is a closed, checkable set, and core
# acts on it. Silently ignoring `affect_scope` would leave an author certain
# they had asked for scope invalidation and wondering, much later, why the
# cache was stale.
trait = {
    id = shuttered
    prop_def = {
        shuttered = { type = bool  affect_scope = yes }
    }
}
