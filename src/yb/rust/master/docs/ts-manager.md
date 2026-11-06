# Tserver registry and heartbeats (`master-ts-manager`)

The registry of tservers, their liveness, and the leader's side of `TSHeartbeat`, field for field
as `MasterHeartbeatServiceImpl::TSHeartbeat`.

## Registration and liveness

- A tserver is `(permanent_uuid, instance_seqno)`. A heartbeat with an unknown uuid or another
  seqno gets `needs_reregister` and `needs_full_tablet_report`.
- Registration refuses an older seqno and a host/port already registered by another uuid with a
  seqno at least as new (`needs_reregister`), and then changes nothing; with a newer seqno the
  other entry becomes REPLACED.
- A peer in a reported Raft config that never heartbeated is registered from the config
  (`enable_register_ts_from_raft`): UNRESPONSIVE, with no xCluster-guarded lease.
- The `tserver-liveness` controller marks a LIVE tserver UNRESPONSIVE after
  `tserver_unresponsive_timeout_ms` without a heartbeat and forgets its report state, so it sends
  a full report when it comes back.
- With the `persist_tserver_registry` AutoFlag, registrations are `TSERVER_REGISTRATION` rows
  (`SysTabletServerEntryPB`, `persisted` set), written when they change, deleted by
  `RemoveTabletServer` and loaded when a term starts. A row that fails to write is retried by the
  liveness controller.
- At the start of a term every tserver must send a full report, and every registered tserver gets
  a fresh heartbeat time, so none is declared unresponsive by a new leader that has not heard from
  it yet. Locations of tablets no longer active and deletes queued in an earlier term are dropped.

## The heartbeat response (L7)

Each field has one owner:

| Fields | Owner |
|---|---|
| `master_config`, readiness, `leader_master` | `master-rpc-handlers` |
| `cluster_uuid`, universe check, `cluster_config_version`, `cluster_replication_info`, `consumer_registry`, `tablet_report_limit`, `tablet_report`, `needs_*`, `tservers`, `live_tservers_*`, `transaction_tables_version` | this crate |
| `universe_key_registry`, `snapshots_info`, `auto_flags_config` | the host's C++ objects, through the `HostHeartbeatParts` contributor (`master-server`) |

`xcluster_enabled_on_producer` is set once CDC streams exist and stays set, as `cdc_enabled_` does
in C++. Other components add `HeartbeatContributor`s. Not filled in layer 2: YSQL catalog versions,
invalidation messages and history pins (W9), xCluster safe times and guarded info (W11), the
leader-blacklist drain list (W8).

## Tablet reports (L6)

The C++ contract is kept: until a tserver has completed a full report only full reports are
accepted; processing stops `heartbeat_rpc_timeout_ms` * `heartbeat_safe_deadline_ratio` before
the deadline with `processing_truncated`; a stale or non-member committed config is ignored.

Observed state goes to `master-tablet-locations` at once: a newer committed config, or one that
differs from the replica map, replaces the tablet's replicas with the config's peers. Catalog
consequences (a newer committed Raft config or leader, a tablet turning RUNNING) become one
`Mutation` per report, group-committed with every other mutation, where C++ writes one Raft op per
changed tablet (`catalog_manager_report_batch_size` = 1). A tablet is deleted when DELETED or
REPLACED, and a table is running when PREPARING, RUNNING or ALTERING, as in C++.

Replicas of deleted tablets and tables, evicted replicas (`ShouldDeleteNonMember`, with the
reported pending config when `use_tablet_report_pending_config_op_id` is on) and hidden tablets get
`DeleteTablet` through the `replica-deletes` task. Each delete carries the `LeaderToken` it was
decided under and is sent only while that token is current; like C++ `DeleteReplica` tasks, it is
retried until `unresponsive_ts_rpc_timeout_ms` with no attempt limit. A replica of a tablet the
catalog never had is only logged, as with `master_enable_deletion_check_for_orphaned_tablets`.

## The versioned live tserver list (L8, Q3)

The leader sends `live_tservers_version = (term, counter)` with the list; the counter changes
whenever the live set or a registration in it changes. A tserver echoes the version it holds, and
when it matches the leader answers `live_tservers_unchanged` and omits the list. A tserver that
sends no version (an older release, or a rollback) always gets the full list, and a C++ master
ignores the field and always sends the list, so there is no AutoFlag. The term makes a version from
an earlier leader never match.

On the tserver (`TabletServer::PopulateLiveTServers`), a response marked unchanged keeps the held
list; any other response replaces it and records its version (or none).
