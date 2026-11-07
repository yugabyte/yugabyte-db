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

## Leaderless tablets (K7)

`record_leader_lease` keeps, per tablet, the lease its current leader reported, with C++'s rules: a
report counts as a valid leader when its expiration does not go backwards for the same leader (an
RF1 leader's infinite expiration is superseded by any later one) and is not more than
`maximum_tablet_leader_lease_expired_secs` in the past. Reports from a tserver the locations do not
name as leader are ignored.

`leaderless` reports a tablet once it has gone without a valid leader for more than the alert delay
of observable time. C++ counts from the last valid report to now, so heartbeats queued on a busy
master, or a new leader that has not heard from tservers, look like outages. Here the window starts
no earlier than the caller's `observable_from` (the term's start plus a grace period), or than when
the locations first had the tablet (C++ counts a new tablet from its creation). While the leader's
tserver is live, the window ends at the latest heartbeat of that tserver whose leases were recorded:
one that skipped them, for lack of time or as part of a full report, is no evidence. A tablet whose
leader's tserver is unresponsive is still reported.
