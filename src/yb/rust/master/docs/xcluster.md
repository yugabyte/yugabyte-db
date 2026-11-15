# xCluster (`master-xcluster`)

xCluster on a Rust leader (W11), over the streams of [master-xrepl](xrepl.md). This covers the
source universe's DB-scoped outbound replication groups; the target's inbound groups are not served
yet.

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
(`XreplExtension`). A failure leaves the table PREPARING until the next pass.

A dropped table with xCluster streams is hidden rather than deleted (`Retainer::active_cdcsdk`,
through the same extension) until its streams are deleted, by the target dropping its table or by
the controller once the table has been hidden for `cdc_wal_retention_time_secs`.

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

## Tests

`cargo test -p master-xcluster`: a group's namespace checkpointed to READY, refusals, a new table's
stream at op 0, deleting a group and removing a namespace, the repair RPCs, paused streams in
heartbeats, a dropped table kept until its streams expire, and the target's setup from the ready
streams, against a fake host, tserver and target.
