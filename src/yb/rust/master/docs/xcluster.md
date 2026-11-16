# xCluster (`master-xcluster`)

xCluster on a Rust leader (W11), over the streams of [master-xrepl](xrepl.md): the source
universe's DB-scoped outbound replication groups and the target universe's inbound replication
groups. `SetupNamespaceReplicationWithBootstrap` and its `IsDone` are never served (see the
[README](README.md#rpcs-a-rust-leader-serves)).

## Outbound replication groups

A group is one `XCLUSTER_OUTBOUND_REPLICATION_GROUP` row (`SysXClusterOutboundReplicationGroupEntryPB`):
each namespace's tables, each with its stream and whether it is still being checkpointed. C++ holds
the same rows in `XClusterOutboundReplicationGroup` objects with a mutex; here every change rebuilds
the row from the latest snapshot in one write, together with the stream rows it creates.

| RPC | Behavior |
|---|---|
| `XClusterCreateOutboundReplicationGroup` | Each namespace's eligible tables (`IsTableEligibleForXClusterReplication`) join as CHECKPOINTING |
| `XClusterAddNamespaceToOutboundReplicationGroup` | The same for one namespace; refused while a checkpoint task of the group runs |
| `XClusterRemoveNamespaceFromOutboundReplicationGroup`, `XClusterDeleteOutboundReplicationGroup` | With target masters, the target's inbound group drops the namespace or is deleted first; the streams are deleted (forced), in automatic mode the DDL replication extension is dropped, then the row changes |
| `IsXClusterBootstrapRequired`, `GetXClusterStreams` | `not_ready` until the namespace is READY; `GetXClusterStreams` by table names, table ids (creating a stream for a table that still has a WAL anchor, with `create_stream_if_missing`) or all |
| `GetXClusterOutboundReplicationGroups`, `GetXClusterOutboundReplicationGroupInfo` | From the rows |
| `RepairOutboundXClusterReplicationGroupAddTable`, `...RemoveTable` | A table joins with an existing xCluster stream, or leaves (its WAL anchor stream is deleted) |
| `DeleteXClusterWalAnchorStreams` | The WAL anchor streams of tables whose creation the target committed |
| `CreateXClusterReplication`, `IsCreateXClusterReplicationDone`, `AddNamespaceToXClusterReplication`, `IsAlterXClusterReplicationDone` | The target sets up (or alters, as `<group>.ALTER`) its inbound group from the READY namespaces' initial tables and streams; the outcome becomes the group's target state, a failure clears it |
| `XClusterEnsureSequenceUpdatesAreInWal` | Through the host (`ScanSequencesDataTable`, `EnsureSequenceUpdatesAreInWal`) |
| `PauseResumeXClusterProducerStreams` | The xCluster config's paused streams, version bumped |

## Checkpointing

A CHECKPOINTING namespace has one task per term, started by the controller each pass (C++
`StartPostLoadTasks` after a leader change). In automatic mode it creates `sequences_data` and adds
the namespace's alias of it, advances the OID counters and invalidates tserver OID caches, creates
the `yb_xcluster_ddl_replication` extension (`AdminExecutePgsql` on the closest tserver) and sets the
namespace's role AUTOMATIC_SOURCE, then marks `ddl_queue` as an initial table. It then creates an
INITIATED transactional stream for each initial table and has the tserver leading the first
table's first tablet bootstrap a batch of them (`BootstrapProducer` with their ids, which writes
their `cdc_state` rows at the end of the WAL), until none is left; the namespace is READY once its
tables are unchanged. Retryable errors back off from 100ms to 5s; any other error makes the
namespace FAILED, which only removing it or deleting the group clears.

A table created in a group's namespace gets its stream, and in automatic mode a WAL anchor stream,
checkpointed at op 0, before it runs: table-lifecycle's assigner calls
`XreplHooks::post_tablet_create`, which master-xrepl forwards to master-xcluster
(`XreplExtension`). The hook spawns one task per table and term and answers `false` until it is
done; the table stays PREPARING meanwhile. A failure is kept by table-lifecycle and returned by
`IsCreateTableDone` with `done = true`: a client that only saw "not done" would wait out its whole
timeout. `disable_xcluster_db_scoped_new_table_processing` skips this work on both sides.

A dropped table with xCluster streams is hidden rather than deleted (`Retainer::active_cdcsdk`,
through the same extension) until its streams are deleted, by the target dropping its table or by
the controller once the table has been hidden for `cdc_wal_retention_time_secs`.

## Inbound replication groups

A group is one `UNIVERSE_REPLICATION` row plus its entry in the cluster config's
`consumer_registry.producer_map` (the stream-to-tablet mapping tservers poll by); setup writes both
in one write, with the cluster config version bumped. An alter runs as a separate `<group>.ALTER`
setup whose tables are merged into the group when it finishes.

| RPC | Behavior |
|---|---|
| `SetupUniverseReplication`, `IsSetupUniverseReplicationDone` | Arguments are checked synchronously (`ValidateInputArguments`, including that a target namespace is in one group only); the rest runs as a task: AutoFlags compatibility, sequences and the DDL extension in automatic mode, then per table the source schema, the target table, the stream (given, or created on the source) and the tablet mapping (host `XClusterTabletMapping`). The outcome is reported once, then a new group is done when it is ACTIVE, has no replication errors and (DB-scoped) has a safe time |
| `AlterUniverseReplication` | Adds or removes tables or namespaces, renames the group, or changes source master addresses |
| `DeleteUniverseReplication` | Removes the registry entry and the row; DB-scoped groups keep the source streams and reserve OIDs above the source's |
| `SetUniverseReplicationEnabled` | Disables or enables the group's streams and waits until every tserver reports them paused |
| `GetUniverseReplication(s)`, `GetUniverseReplicationInfo`, `GetReplicationStatus` | From the row, the registry and the tservers' last reports |
| `GetXClusterSafeTime`, `GetXClusterSafeTimeForNamespace` | From the `XCLUSTER_SAFE_TIME` row |
| `UpdateConsumerOnProducerSplit`, `UpdateConsumerOnProducerMetadata` | Registry updates the source's tservers send: a split source tablet's children, schema version mappings |
| `XClusterStopPersisting`, `XClusterStartPersisting`, `XClusterGetPersistenceBlockMap`, `XClusterReportNewAutoFlagConfigVersion` | Guarded-info persistence and AutoFlags revalidation |
| `HandleNewSchemaForAutomaticXClusterTarget`, `InsertPackedSchemaForXClusterTarget`, `InsertHistoricalColocatedSchemaPacking` | Schema packings the target's DDL replication needs before it applies a source DDL |
| `BootstrapProducer`, `IsBootstrapRequired`, `WaitForReplicationDrain` | Legacy table-level setup helpers, run on the source |

Setup tasks, replication status and the safe times being computed are in memory, as in C++: a new
leader forgets a running setup (the client retries) and rebuilds status from the next heartbeats.

On the target, a table created in a DB-scoped group's namespace joins the group in its post-create
work: the source's stream for it (`GetXClusterStreams`), then an alter that adds it, then a wait for
the safe time to cover it. In a transactional table-level group, an index created on a replicated
table joins instead through the source's `BootstrapProducer` for the index by name, and the safe
time must pass the bootstrap time: the target's backfill covers rows written before it. `CreateTable` in automatic mode copies the source table's xCluster info
and historical packings into the new table (`ProcessCreateTableReq`).

The controller (`XClusterController`) runs on every catalog change and on the safe time interval:
checkpoint tasks, dropped target tables leaving their groups, streams of old hidden tables, split
parents the target has finished with (released to the hidden-objects cleanup), AutoFlags
revalidation every 5s, and the safe time: each namespace's minimum over its producer tablets in
`system.xcluster_safe_time` (read and cleaned up through host calls), written to the
`XCLUSTER_SAFE_TIME` row. It also deletes, once per term, the groups an interrupted setup,
alter or delete left behind (FAILED, DELETED_ERROR, DELETING or `.ALTER`; C++
`ClearFailedUniverse`), and every `xcluster_cleanup_tables_frequency_secs` it deletes old rows of
the DDL replication tables in automatic-mode databases.

## Index backfill

`XreplHooks::backfill_decision` picks a backfill's read time (`backfill.rs`, C++
`TryGetXClusterInfoForIndexBackfill`): on an automatic-mode target, the hybrid time the ddl_queue
handler stored on the indexes, or no local backfill at all when every index's stream uses the
target-applied filter (the source's backfill writes are replicated); otherwise the namespace's
safe time for a consumer table, or the tablets' safe time. `backfill_completed` then clears the
indexes' `xcluster_table_info` and, on a source, moves their streams to the end of the WAL, except
target-applied-filter streams and WAL anchors. Bidirectional index auto-add is not served.

## Failover

`XClusterFailover` on an automatic-mode target sets the group's `failover_in_progress` and starts a
task (`failover.rs`, C++ `XClusterFailoverTask`): it pauses the group and waits for every tserver
to report it paused, disables tablet splitting until in-flight splits finish, computes the safe
time, then snapshots each namespace, restores each snapshot at its namespace's safe time, deletes
the group (keeping the source streams, reserving no OIDs) and deletes the snapshots. A failure
deletes the snapshots and records the error in `last_failover_completion_status`.
`IsXClusterFailoverDone` reads the row: a deleted group means success. The snapshot coordinator and
split manager are reached through `FailoverSnapshots`, which the server wires. Progress is in
memory, as in C++: the controller marks a running failover no task of the current term owns as
aborted, and the client starts over.

## The other universe (K9)

`RemoteMasters::call` sends one call to the other universe's leader master through the host's
`XClusterRemoteCall`, which keeps a `XClusterRemoteClientHolder` per replication group (its
certificates come from `certs_for_cdc_dir`) and forwards the serialized request. Leader changes and
transport errors are retried in Rust until the deadline (`call_leader`).

## Heartbeats

`XCluster` is a heartbeat contributor (`XClusterConfig::FillHeartbeatResponse`): the producer
registry when the tserver's `xcluster_config_version` is older, the namespace roles in the
deprecated heartbeat info unless `skip_fields_moved_to_xcluster_guarded_info`, and the target's
safe times from the `XCLUSTER_SAFE_TIME` row.

## Host calls

| Method | Purpose |
|---|---|
| `XClusterRemoteCall` | One call to the leader master of another universe |
| `CreateSequencesDataTable` | `tserver::CreateSequencesDataTable` through the host's client |
| `EnsureSequenceUpdatesAreInWal` | `ScanSequencesDataTable` and `EnsureSequenceUpdatesAreInWal` per database |
| `XClusterValidateAutoFlags`, `LocalAutoFlagsConfigVersion` | AutoFlags compatibility with the source |
| `XClusterTabletMapping`, `XClusterProducerSplit`, `XClusterConsumerSplit` | The registry's producer-to-consumer tablet mapping, as C++ builds it from partitions |
| `XClusterSchemasEquivalent` | Source and target schema comparison |
| `XClusterSafeTimeCreateTable`, `XClusterSafeTimeRead`, `XClusterSafeTimeDelete` | `system.xcluster_safe_time` |
| `XClusterHistoricalSchemaPacking` | A colocated table's historical schema packing |

## Tests

`cargo test -p master-xcluster`: a group's namespace checkpointed to READY, refusals, a new table's
stream at op 0 (and none while new-table processing is disabled), deleting a group and removing a
namespace, the repair RPCs, paused streams in heartbeats, a dropped table kept until its streams
expire, the target's setup from the ready streams and its refusal reaching the source, a target
namespace in one inbound group only, replication status from tserver reports, and failover refusals
and the stale-failover cleanup, against a fake host, tserver and target. End to end: `xcluster_db_scoped-test` with `YB_RUST_MASTER=1`.
