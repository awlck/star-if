# starvfs

Layered virtual filesystem (`docs/proposal.md` §2.1, §12.5, §14.1). Backlog
workstream G, Phase 0 -- independent of `stardata`, and the backlog notes the
*implementation* can be deferred entirely without affecting the phase exit
criterion.

The API's shape landed ahead of that implementation, per the backlog's own
recommendation not to defer it: `include/starvfs/` holds fully commented
headers (`Path`, `Result`/`Future`/`Promise`, `HostIo`, `Layer` and its three
implementations, the `Vfs` mount stack), with `Path`, `Result`, `Future` and
`Layer::exists` implemented for real and everything else stubbed to fail with
`Error::NotImplemented`. See `docs/starvfs-api.md` for the rationale and
`docs/phase-0-backlog.md`'s workstream G for what's scheduled (G2 onward:
the directory layer, the zip layer, and the mount stack's resolution logic).
