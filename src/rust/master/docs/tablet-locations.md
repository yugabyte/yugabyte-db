# Tablet locations (`master-tablet-locations`)

Observed replica state: each tablet's replicas, their Raft roles and member types, as tserver
reports say. It is not catalog state and is never written to the sys catalog. Sharded by tablet id
(64 shards); per-tserver replica and leader counts are kept as entries change.

Each entry is `confirmed` once a report in the current term set it. A new leader marks everything
unconfirmed; `GetTableLocations` still serves it.

## The location stream (L9)

C++ followers keep no locations, so every C++ failover starts cold until tservers report. A Rust
leader instead pushes locations and tserver registrations to Rust followers over
`yb.master.MasterRustPeer.PushLocations` (`src/yb/master/master_rust_peer.proto`):

- every `rust_master_location_push_interval_ms` (500 ms), to every peer at once, the tablets
  changed since the last push and, when membership changed, every tserver;
- a per-follower sequence number within the term; a follower that sees a gap or a new term answers
  `needs_full`, and the next push to it is everything;
- a push that fails is followed by a full push, after a backoff that doubles from the push
  interval up to 30 s; a round builds at most one full payload;
- a C++ peer answers "Service yb.master.MasterRustPeer not registered" and gets no more pushes in
  the term.

A follower refuses pushes while it leads, and ignores a push from a term older than its own Raft
term or than the last push it applied: a deposed leader may not have noticed yet.

A follower applies pushed locations as unconfirmed and keeps pushed tservers in its registry, so
right after a failover it answers `GetTableLocations` and `ListTabletServers` before any tserver
has reported. The new leader still asks every tserver for a full report on its first heartbeat.
