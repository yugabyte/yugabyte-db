---
title: Key concepts - logical replication
headerTitle: Key concepts
linkTitle: Key concepts
description: Logical replication in YugabyteDB.
headcontent: PostgreSQL logical replication concepts
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/key-concepts/
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: key-concepts
    weight: 10
type: docs
---

The YugabyteDB logical replication feature uses [PostgreSQL Logical Replication](https://www.postgresql.org/docs/15/logical-replication.html), which operates using a publish-subscribe model. Understanding the following key concepts will help you set up and manage a logical replication environment effectively.

## Concepts

### Replication slot

A replication slot represents a stream of changes that can be replayed to a client in the order they were made on the origin server. Each slot streams a sequence of changes from a single database.

In logical replication, the fundamental unit of data transmission is a transaction. A logical slot emits each change just once in normal operation. The current position of each slot is persisted only at checkpoint, so if a replication process is interrupted and restarts, even if the checkpoint or the starting Log Sequence Number ([LSN](#lsn-type)) falls in the middle of a transaction, **the entire transaction is retransmitted**. This behavior guarantees that clients receive complete transactions without missing any intermediate changes, maintaining data integrity across the replication stream​. Logical decoding clients are responsible for avoiding ill effects from handling the same message more than once. Clients may wish to record the last LSN they saw when decoding and skip over any repeated data or (when using the replication protocol) request that decoding start from that LSN rather than letting the server determine the start point.

For more information, refer to [Replication slots](https://www.postgresql.org/docs/15/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS) in the PostgreSQL documentation.

#### LSN type

A Log Sequence Number (LSN) in YugabyteDB differs from what you may be accustomed to in PostgreSQL. In PostgreSQL, an LSN represents a specific 'location' in the WAL, and has significance that spans databases and replication slots. In YugabyteDB, an LSN uniquely identifies a change event, and the LSN is valid only in the context of a specific replication slot. Due to these differences, there are inherent limitations in how LSNs can be used.

You can specify the type of LSN to use when you create a replication slot. YugabyteDB currently supports the following types:

- SEQUENCE - (Default) PostgreSQL-style LSN that is valid in the context of a slot. It is a monotonic increasing number that determines the record in global order in the context of a slot. It can't be compared across two different slots.
- HYBRID_TIME - A hybrid time value which can be used natively with YugabyteDB. HYBRID_TIME is denoted by the HybridTime of the transaction commit record. All the records of the transaction that is streamed will have the same LSN as that of the commit record. You need to ensure that the changes of a transaction are applied in totality and the acknowledgement is sent only if the commit record of a transaction is processed.

#### Slot scope

A replication slot belongs to one database and is not tied to a publication. The client names the publication when it starts streaming (the `publication_names` option of `START_REPLICATION`), so the same slot can be started with different publications over time, as in PostgreSQL.

When you create a slot, YugabyteDB establishes a [consistent point](#consistent-point) for the database and starts retaining changes for every table in the database, not only for the tables in a publication. Tables created after the slot are added to the slot automatically. The publication only selects which of those tables are streamed to the client.

If the client starts streaming without naming a publication, YugabyteDB streams the changes of all tables in the database, as if a `FOR ALL TABLES` publication had been used. This applies to `test_decoding` and `wal2json`. `pgoutput` and `yboutput` require the `publication_names` option and fail without it, as in PostgreSQL.

#### Tables of interest

Retaining changes for a table has a cost. Every tablet of the table keeps its WAL and transaction intents until the slot consumes them, and compaction is held back if the slot needs before images. To avoid paying that cost for tables a slot never reads, YugabyteDB tracks whether each tablet is _of interest_ to the slot:

- A tablet becomes of interest the first time the slot polls it. The slot polls every tablet of every table in the publication, whether or not the table has changes.
- A tablet that has not been polled within [cdcsdk_tablet_not_of_interest_timeout_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-tablet-not-of-interest-timeout-secs) (default 4 hours) of slot creation is marked as not of interest. Its retention barriers are released, and the slot stops tracking it.
- Tablets of tables created after the slot are always of interest.

A table whose tablets are not of interest, or have [expired](#when-a-slot-becomes-unusable), is _unqualified_ for the slot. This has the following implications:

- The publication you use when you first start streaming decides which tables the slot can stream later, because a table left out of it for the first 4 hours after slot creation becomes unqualified: its changes are no longer retained for the slot, and adding it to the publication later does not bring it back. Adding it while a client is streaming makes the stream fail with the error below until the table is removed from the publication. If this is not resolved within `cdc_intent_retention_ms`, the slot expires. To stream the table, create a new slot.
- Start streaming within 4 hours of creating a slot. If no client connects in that window, every table becomes unqualified and the slot can't be used.
- If the publication contains an unqualified table, at start or after an `ALTER PUBLICATION ... ADD TABLE` while streaming, streaming fails with the error `Cannot add tablet: <id> to the polling list as it has been unqualified for stream: <id>`. Either remove the table from the publication, or set the [cdc_skip_unqualified_tables_for_polling](../../../../reference/configuration/yb-tserver/#cdc-skip-unqualified-tables-for-polling) flag (v2025.2.7.0, v2026.1.2.0, and later) so that the slot skips unqualified tables and streams the rest.

#### Restart time

The restart time of a slot is the commit time from which streaming resumes when the client reconnects. It is also the point up to which the database must retain changes for the slot. `pg_replication_slots` reports it as `restart_lsn` and as `yb_restart_commit_ht` (a hybrid time). From v2025.1, `yb_restart_time` reports the same value as a timestamp. `confirmed_flush_lsn` is the last LSN the client acknowledged.

The restart time advances in the following cases:

- When the client acknowledges an LSN (a standby status update with the flushed LSN). The restart time moves to the commit of the latest transaction whose `COMMIT` record is at or before the acknowledged LSN. Acknowledging an LSN in the middle of a transaction does not move the restart time past the previous commit. With the HYBRID_TIME LSN type, acknowledge only the LSN of a `COMMIT` record, after applying the whole transaction.
- When the slot is idle. If the client has acknowledged everything it received and there is nothing new to stream, the restart time advances to the slot's safe time every 60 seconds, so that an idle slot does not accumulate retained changes. This is controlled by the `cdcsdk_update_restart_time_when_nothing_to_stream` and `cdcsdk_update_restart_time_interval_secs` flags.

The restart time does not advance while the client is disconnected, or while a connected client receives changes without acknowledging them. In both cases the retained WAL and intents keep growing on every tablet the slot polls.

The slot state is stored in the `cdc_state` system table together with a checkpoint for each tablet, so it survives client reconnections, walsender restarts, and node failures.

#### Disconnection and reconnection

When the client disconnects, the walsender exits, and the in-memory state of the slot on the YB-TServer is discarded. The slot itself, its restart time, and the per-tablet checkpoints remain. After 60 seconds without consumption (the `ysql_cdc_active_replication_slot_window_ms` flag) `pg_replication_slots` shows the slot as not `active`.

While the client is disconnected, every tablet the slot polls keeps retaining WAL, transaction intents, and, if the slot needs before images, row history from the slot's restart time. Nothing is released before the client reconnects and acknowledges changes, unless the slot expires first. The retention lasts up to [cdc_intent_retention_ms](../../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms) (default 8 hours), measured from the last successful poll of each tablet. After that the slot expires; see [When a slot becomes unusable](#when-a-slot-becomes-unusable). The matching WAL retention is set by [cdc_wal_retention_time_secs](../../../../reference/configuration/yb-tserver/#cdc-wal-retention-time-secs) (default 8 hours), which must be at least as long.

When the client reconnects, streaming resumes from the restart time. Every transaction committed after the restart time is sent again, in full, including transactions the client received but had not acknowledged before it disconnected. The records keep the LSNs they had before (see [Determinism](#lsn)), so the client can detect and skip duplicates.

#### When a slot becomes unusable

A slot stops working in the following cases:

- **Expired.** A tablet of the slot was not polled for `cdc_intent_retention_ms` (default 8 hours), for example because the client was disconnected. The retention barriers are released, intents are garbage collected, and the next poll fails with `Stream ID <id> is expired for Tablet ID <id>`. From v2025.1, `pg_replication_slots` shows `wal_status` as `lost` when no tablet of the slot has been polled for that long. The column does not reflect the expiry of individual tablets or tables that are not of interest, so a slot can show `reserved` and still fail to start.
- **Unqualified table in the publication.** A table that is [not of interest](#tables-of-interest) is in the publication, at start or added while streaming. Streaming fails unless the table is removed from the publication or `cdc_skip_unqualified_tables_for_polling` is set.
- **Database dropped.** Dropping the database drops its slots.

An expired slot can't be recovered. Drop the slot and create a new one. The new slot has a new [consistent point](#consistent-point), so the consumer must take a new [initial snapshot](#initial-snapshot) as well.

For how this behavior has changed across releases and how it compares with PostgreSQL, see [Slot behavior by version](../slot-behavior-by-version/).

### Initial snapshot

A replication slot streams only the transactions committed after the slot was created. A client that also needs the rows that existed before that point takes an initial snapshot of the tables first, and then streams the changes from the slot. As in PostgreSQL, the snapshot is not part of the replication protocol. The client reads the tables with ordinary `SELECT` queries on a regular connection, and the database guarantees that the rows it reads and the changes it streams line up exactly.

#### Consistent point

When a slot is created, YB-Master chooses a hybrid time, the _consistent point_, that separates the snapshot from the stream for every table in the database:

- Rows as they were at the consistent point belong to the snapshot.
- Transactions with a commit time after the consistent point are streamed, in commit order.

The consistent point is the slot's initial [restart time](#restart-time), reported as `yb_restart_commit_ht` in `pg_replication_slots`. `CREATE_REPLICATION_SLOT` returns it in the `snapshot_name` field: as an exported snapshot name in v2025.2.2 and later (the default `EXPORT_SNAPSHOT` option), or as the hybrid time itself in earlier releases and with `USE_SNAPSHOT`.

#### Taking the snapshot

To read a table as of the consistent point, the client pins a transaction to that time and runs the snapshot `SELECT` in the same transaction, in one of two ways:

- `SET TRANSACTION SNAPSHOT '<snapshot_name>'` in a `REPEATABLE READ` transaction, using the exported snapshot name returned by `CREATE_REPLICATION_SLOT`. As in PostgreSQL, the exported snapshot is valid only until the replication connection that created the slot runs its next command. Available from v2025.2.2.
- `SET LOCAL yb_read_time TO '<hybrid time> ht'`, using the hybrid time from `yb_restart_commit_ht`, or from `snapshot_name` if the slot was created with `USE_SNAPSHOT`. Set `yb_disable_catalog_version_check` to true in the session first; otherwise `yb_read_time` can't be set inside a transaction block. This is also how a client reads the snapshot of a slot that already exists, as long as the slot still [retains history](#retention-during-the-snapshot) at the consistent point. It requires superuser privileges or the setup in [Initial snapshot](../advanced-topic/#initial-snapshot).

The query reads the table as it was at the consistent point. It takes no locks and does not block writes. Rows from transactions that committed after the consistent point are not returned; their changes arrive in the stream. Every table, and every connection that uses the same time, sees the same consistent state, so tables can be read on separate connections and in parallel. This differs from PostgreSQL, where Debezium holds table locks during the snapshot (`snapshot.locking.mode`).

The [YugabyteDB connector](../yugabytedb-connector/#snapshots) takes the snapshot before it starts streaming, when `snapshot.mode` is `initial` (the default) or `initial_only`. It pins a read-only transaction as described above, and runs one `SELECT` per captured table, fetching rows in batches of `snapshot.fetch.size` and emitting a `READ` event for each row. Tables are read one at a time. The query is a plain full-table `SELECT`; `snapshot.select.statement.overrides` is the only way to filter rows. The snapshot is not resumable: if the connector restarts before the snapshot completes, it starts the snapshot again from the first table, as of the same consistent point. Streaming starts after the last table, from the consistent point. To snapshot and stream a large table with several tasks, see [Parallel streaming](../yugabytedb-connector/#parallel-streaming).

#### Retention during the snapshot

A snapshot can take a long time, and the slot is already retaining data while it runs:

- Row versions as of the consistent point are retained on every tablet of the database, whatever the replica identity of the table, so that the snapshot query can read them. They are released when the slot's restart time moves past the consistent point, which happens when the client acknowledges streamed changes. This holds for slots created with `CREATE_REPLICATION_SLOT` using `EXPORT_SNAPSHOT` (the default) or `USE_SNAPSHOT`. A slot created with `NOEXPORT_SNAPSHOT`, or with the `pg_create_logical_replication_slot()` function, retains history only for tables whose replica identity needs before images. For its other tables, a snapshot query as of the consistent point is not guaranteed to succeed once older row versions are compacted away, after `timestamp_history_retention_interval_sec` (default 15 minutes).
- WAL and transaction intents accumulate on every tablet from slot creation until the client starts streaming and acknowledges changes, as described in [Restart time](#restart-time).
- The timeouts in [Tables of interest](#tables-of-interest) and [When a slot becomes unusable](#when-a-slot-becomes-unusable) count from slot creation, not from the start of streaming. Streaming must start within `cdcsdk_tablet_not_of_interest_timeout_secs` (default 4 hours) of slot creation, or every table becomes unqualified and the slot can't be used, and the slot expires after `cdc_intent_retention_ms` (default 8 hours) without a poll. If the snapshot is expected to take longer, raise these flags, and `cdc_wal_retention_time_secs` to match, before creating the slot.
- Do not run DDL on the tables being snapshotted between slot creation and the end of the snapshot. Reading a table as of a time before a DDL on it is not supported.

### Publication

A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. Each publication exists in only one database.

Publications are different from schemas and do not affect how the table is accessed. Each table can be added to multiple publications if needed. Publications may currently only contain tables. Objects must be added explicitly, except when a publication is created for ALL TABLES.

Changing a publication while a slot is streaming takes effect at commit time with [implicit publication](../advanced-topic/#implicit-publication), the default from v2026.1, or at the next refresh with [periodic publication](../advanced-topic/#periodic-publication), the only mode in earlier releases. A slot's mode is fixed when the slot is created. See [Adding tables to publication](../advanced-topic/#adding-tables-to-publication) for details, and [Slot behavior by version](#slot-behavior-by-version) for the versions.

For more information, refer to [Publication](https://www.postgresql.org/docs/15/logical-replication-publication.html#LOGICAL-REPLICATION-PUBLICATION) in the PostgreSQL documentation.

### Output plugin

Output plugins transform the data from the write-ahead log's internal representation into the format that can be consumed by replication clients. These plugins are notified about the change events that need to be processed and sent via various callbacks. These callbacks are only invoked when the transaction actually commits.

YugabyteDB supports the following four output plugins:

- `yboutput`
- `pgoutput`
- `test_decoding`
- `wal2json`

All these plugins are pre-packaged with YugabyteDB and do not require any external installation.

{{< note title="Note" >}}

The plugin `yboutput` is YugabyteDB specific. It is similar to `pgoutput` in most aspects. The only difference being that replica identity `CHANGE` is not supported in `pgoutput`. All other plugins support replica identity `CHANGE`.

{{</note>}}

For more information, refer to [Logical Decoding Output Plugins](https://www.postgresql.org/docs/15/logicaldecoding-output-plugin.html) in the PostgreSQL documentation.

### LSN

LSN (Log Sequence Number) in YugabyteDB is an unsigned 64-bit integer that uniquely identifies a change record or a transaction boundary record that is consumed from a given replication slot.

In YugabyteDB, LSN values from different slots are considered unrelated and should not be compared. In YugabyteDB, LSN no longer represents the byte offset of a WAL record.

LSN values for a single replication slot satisfy the following properties:

- **Uniqueness**

    LSN values for the change and `COMMIT` records for a given replication slot are unique. In particular, changes from different tablets of the same or different tables will have unique LSN values for a replication slot.

- **Ordering**

    LSN values can be compared ( `<`, `>`, `=` ).

    The LSN of the change records in a transaction will be strictly lower than the LSN of the COMMIT record of the same transaction.

    The LSNs of change records in a transaction will be in increasing order and will correspond to the order in which those changes were made in that transaction. That is, the LSN of an earlier change will have a strictly lower value than the LSN of a later change in the same transaction. This is the case even if the changes correspond to rows in different tablets of the same or different tables.

    For a given replication slot, the LSN of a `COMMIT` record of an earlier transaction will be strictly lower than the LSN of the `COMMIT` record of a later transaction.

- **Determinism**

    For a given replication slot, the LSN value of a change record (or a transaction boundary record) remains the same for the lifetime of that replication slot. In particular, this is true across server and client restarts and client re-connections. Thus, LSN values for a single replication slot may be used to uniquely identify records that are consumed from that replication slot. The values can be compared for determining duplicates at the client side.

### Replica identity

Replica identity is a table-level parameter that controls the amount of information being written to the change records. YugabyteDB supports the following four replica identities:

- CHANGE (default)
- DEFAULT
- FULL
- NOTHING

The PostgreSQL replica identity `INDEX` is not supported in YugabyteDB.

Replica identity `CHANGE` is the best performant and the default replica identity. The replica identity of a table can be changed by performing an ALTER TABLE. However, for a given slot, any ALTER TABLE performed to change the replica identity after the creation of the slot will have no effect. This means that the effective replica identity for any table for a slot, is the replica identity of the table that existed at the time of slot creation. A dynamically created table (a table created after slot creation) will have the default replica identity. For a replica identity modified after slot creation to take effect, a new slot will have to be created after performing the ALTER TABLE.

The [ysql_yb_default_replica_identity](../../../../reference/configuration/yb-tserver/#ysql-yb-default-replica-identity) flag determines the default replica identity for user tables at the time of table creation. The default value is `CHANGE`. The purpose of this flag is to set the replica identities for dynamically created tables. In order to create a dynamic table with desired replica identity, the flag must be set accordingly and then the table must be created.

{{< note title="Advisory" >}}
You should refrain from altering the replica identity of a dynamically created table for at least 5 minutes after its creation.
{{< /note >}}

For more information, refer to [Replica Identity](../yugabytedb-connector/#replica-identity).

For information on replica identity in PostgreSQL, refer to [REPLICA IDENTITY](https://www.postgresql.org/docs/15/sql-altertable.html#SQL-ALTERTABLE-REPLICA-IDENTITY) in the PostgreSQL documentation.

### Replication protocols

PostgreSQL has defined protocols for replication that need to be followed by clients to establish replication connection as well as message structures for streaming data. This includes the [Streaming Replication protocol](https://www.postgresql.org/docs/15/protocol-replication.html) and the [Logical Streaming Replication protocol](https://www.postgresql.org/docs/15/protocol-logical-replication.html).

The logical streaming replication protocol sends individual transactions one-by-one. This means that all messages between a pair of `BEGIN` and `COMMIT` messages belong to the same transaction.

YugabyteDB supports both the streaming replication protocols used in PostgreSQL to support logical replication, maintaining the same semantics described in PostgreSQL:

- Streaming Replication Protocol - This protocol is followed by all output plugins.

- Logical Streaming Replication Protocol - This protocol is followed by `pgoutput` and `yboutput`, in addition to the Streaming replication protocol.

{{< note title="Note" >}}

YugabyteDB does not support Physical Replication.

{{< /note >}}

## Learn more

[CDC using Logical Replication architecture](../../../../architecture/docdb-replication/cdc-logical-replication/)
