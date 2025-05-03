---
title: xCluster
headerTitle: xCluster replication
linkTitle: xCluster
description: xCluster replication between multiple YugabyteDB universes.
headContent: Asynchronous replication between independent YugabyteDB universes
menu:
  stable:
    identifier: architecture-docdb-async-replication
    parent: architecture-docdb-replication
    weight: 300
type: docs
---

## YugabyteDB's xCluster replication

xCluster replication is YugabyteDB's implementation of high throughput asynchronous physical replication between two YugabyteDB universes. It allows you to set up one or more unidirectional replication _flows_ between universes. For each flow, data is replicated from a _source_ (also called a producer) universe to a _target_ (also called a consumer) universe. Replication is done at the DocDB layer, by efficiently replicating WAL records asynchronously to the target universe. Both YSQL and YCQL are supported.

Multiple flows can be configured; for instance, setting up two unidirectional flows between two universes, one in each direction, enables bidirectional replication. This ensures that data written in one universe is replicated to the other without causing infinite loops. Refer to [supported deployment scenarios](#deployment-scenarios) for details on the supported flow combinations.

For simplicity, flows are described as being between entire universes. However, flows are actually composed of streams between pairs of YCQL tables or YSQL databases, one in each universe, allowing replication of only certain tables or databases.

Note that xCluster can only be used to replicate between primary clusters in two different universes; it cannot be used to replicate between clusters in the same universe. (See [universe versus cluster](../../key-concepts/#universe) for more on the distinction between universes and clusters.)

{{< tip >}}
To understand the difference between xCluster, Geo-Partitioning, and Read Replicas, refer to [Multi-Region Deployments](../../../explore/multi-region-deployments/).
{{< /tip >}}

## Synchronous versus asynchronous replication

YugabyteDB's [synchronous replication](../replication/) can be used to tolerate losing entire data centers or regions.  It replicates data in a single universe spread across multiple (three or more) data centers so that the loss of one data center does not impact availability, durability, or strong consistency courtesy of the Raft consensus algorithm.

However, asynchronous replication can be beneficial in certain scenarios:

- **Low write latency**: With synchronous replication, each write must reach a consensus across a majority of data centers. This can add tens or even hundreds of milliseconds of extra latency for writes in a multi-region deployment. xCluster reduces this latency by eliminating the need for immediate consensus across regions.
- **Only two data centers needed**: With synchronous replication, to tolerate the failure of `f` fault domains, you need at least `2f + 1` fault domains. Therefore, to survive the loss of one data center, a minimum of three data centers is required, which can increase operational costs. For more details, see [fault tolerance](../replication/#fault-tolerance). With xCluster, you can achieve multi-region deployments with only two data centers.
- **Disaster recovery**: xCluster uses independent YugabyteDB universes in each region that can function independently of each other. This setup allows for quick failover with minimal data loss in the event of a regional outage caused by hardware or software issues.

Asynchronous xCluster replication has the following drawbacks:

- **Potential data loss**: In the event of a data center failure, any data that has not yet been replicated to the secondary data center will be lost. The extent of data loss is determined by the replication lag, which is typically subsecond but can vary depending on the network conditions between the data centers.
- **Stale reads**: When reading from the secondary data center, there may be a delay in data availability due to the asynchronous nature of the replication. This can result in stale reads, which may not reflect the most recent writes. Non-transactional modes can serve torn reads of recently written data.

{{< tip title="Deploy" >}}
To better understand how xCluster replication works in practice, check out [xCluster deployment](../../../deploy/multi-dc/async-replication/).
{{< /tip >}}

## Asynchronous replication modes

Because there is a useful trade-off between how much consistency is lost and what transactions are allowed, YugabyteDB provides two different modes of asynchronous replication:

- Non-transactional replication. Writes are allowed on the target universe, but reads of recently replicated data can be inconsistent.
- Transactional replication. Consistency of reads is preserved on the target universe, but writes are not allowed.

### Non-transactional replication

All writes to the source universe are independently replicated to the target universe, where they are applied with the same timestamp they committed on the source universe. No locks are taken or honored on the target side.

Due to replication lag, a read performed in the target universe immediately after a write in the source universe may not reflect the recent write. In other words, reads in the target universe do not wait for the latest data from the source universe to become available.

Note that the writes are usually being written in the past as far as the target universe is concerned. This violates the preconditions for YugabyteDB serving consistent reads (see the discussion on [safe timestamps](../../transactions/single-row-transactions/#safe-timestamp-assignment-for-a-read-request)). Accordingly, reads on the target universe are no longer strongly consistent but rather eventually consistent even in a single table.

If both target and source universes write to the same key, then the last writer wins. The deciding factor is the underlying hybrid time of the updates from each universe.

#### Inconsistencies affecting transactions

Due to the independent replication of writes, transactions from the source universe become visible over time. This results in transactions on the target universe experiencing non-repeatable reads and phantom reads, regardless of their declared isolation level. Consequently, all transactions on the target universe effectively operate at the SQL-92 isolation level READ COMMITTED, which only ensures that transactions do not read uncommitted data. Unlike the standard YugabyteDB READ COMMITTED level, this does not guarantee that a statement will see a consistent snapshot or all data committed before the statement is issued.

If the source universe fails, the target universe may be left in an inconsistent state where some source universe transactions have only some of their writes applied in the target universe (these are called _torn transactions_). This inconsistency will not automatically heal over time and may need to be manually resolved.

Note that these inconsistencies are limited to the tables/rows being written to and replicated from the source universe: any target transaction that does not interact with such rows is unaffected.

{{< tip >}}
For YSQL deployments, transactional mode is preferred because it provides the necessary consistency guarantees typically required for such deployments.
{{< /tip >}}

### Transactional replication

This mode is an extension of the previous one.  In order to restore consistency, we additionally disallow writes on the target universe and cause reads to read as of a time far enough in the past (typically 250 ms) that all the relevant data from the source universe has already been replicated.

In particular, we pick the time to read as of, _T_, so that all the writes from all the source transactions that will commit at or before time _T_ have been replicated to the target universe.  Put another way, we read as of a time far enough in the past that there cannot be new incoming source commits at or before that time.  This restores consistent reads and ensures source universe transaction results become visible atomically.  Note that we do _not_ wait for any current in flight source-universe transactions.

In order to know when to read as of, we maintain an analog of safe time called _xCluster safe time_, which is the latest time it is currently safe to read as of with xCluster transactional replication in order to guarantee consistency and atomicity.  xCluster safe time advances as replication proceeds but lags behind real-time by the current replication lag.  This means, for example, if we write at 2 PM in the source universe and read at 2:01 PM in the target universe and replication lag is say five minutes then the read will read as of 1:56 PM and will not see the write.  We won't be able to see the write until 2:06 PM in the target universe assuming the replication lag remains at five minutes.

If the source universe dies, then we can discard all the incomplete information in the target universe by rewinding it to the latest xCluster safe time (1:56 PM in the example) using YugabyteDB's [Point-in-Time Recovery (PITR)](../../../manage/backup-restore/point-in-time-recovery/) feature.  The result will be the fully consistent database that results from applying a prefix of the source universe's transactions, namely exactly those that committed at or before the xCluster safe time.  Unlike with non-transactional replication, there is thus no need to handle torn transactions.

It is unclear how to best support writes in the target universe using this strategy of maintaining consistency by reading only at a safe time in the past: A target update transaction would appear to need to read from the past but write in the present; it would thus have to wait for at least the replication lag to make sure no interfering writes from the source universe occurred during that interval.  Such transactions would thus be slow and prone to aborting.

Accordingly, target writes are not currently permitted when using xCluster transactional replication.  This means that the transactional replication mode cannot support bidirectional replication.

Target-universe read-only transactions are still permitted; they run at serializable isolation level on a single consistent snapshot taken in the past.


## High-level implementation details

At a high level, xCluster replication is implemented by having _pollers_ in the target universe that poll the source universe tablet servers for recent changes.  Each poller works independently and polls one source tablet, distributing the received changes among a set of target tablets.

The polled tablets examine only their Raft logs to determine what changes have occurred recently rather than looking at their RocksDB instances.  The incoming poll request specifies the Raft log entry ID to start gathering changes from and the response includes a batch of changes and the Raft log entry ID to continue with next time.

Pollers occasionally checkpoint the continue-with Raft ID of the last batch of changes they have processed; this ensures each change is processed at least once.

### The mapping between source and target tablets

In simple cases, each target tablet can have a dedicated poller that directly polls the corresponding source tablet. However, in more complex scenarios, the number of tablets in the source and target universes may differ. Even if the number of tablets is the same, their sharding boundaries might not align due to historical tablet splits occurring at different points in time.

This means that each target tablet may require changes from multiple source tablets, and multiple target tablets may need changes from the same source tablet. To prevent redundant cross-universe reads from the same source tablet, only one poller reads from each source tablet. When a source tablet's changes are needed by multiple target tablets, the assigned poller distributes the changes to the relevant target tablets.

The following illustration shows an example of this setup for a single table:

![distribution of pollers and where they pull data from and send it to](/images/architecture/replication/distribution-of-pollers-new.png)

In the illustration, the source universe is depicted on the left with three TServers (white boxes), each containing one tablet of the table (boxes inside) with specified ranges. The target universe is on the right, featuring one fewer TServer and tablet. The data from the top source tablet is distributed among both target tablets by the poller in the top target TServer. Meanwhile, the data from the remaining source tablets is replicated to the second target tablet by the pollers in the other target TServer. For simplicity, only the tablet leaders are shown, as pollers operate at and poll from leaders only.

Tablet splitting generates WAL records, which are replicated to the target side. This ensures that the mapping of pollers to source tablets is automatically updated as needed when a source tablet splits.

### Single-shard transactions

These are straightforward: when one of these transaction commits, a single Raft log entry is produced containing all of that transaction's writes and its commit time.  This entry in turn is used to generate part of a batch of changes when the poller requests changes.

Upon receiving the changes, the poller examines each write to see what key it writes to in order to determine which target tablet covers that part of the table.  The poller then forwards the writes to the appropriate tablets. The commit times of the writes are preserved and the writes are marked as _external_, which prevents them from being further replicated by xCluster, whether onward to an additional cluster or back to the cluster they came from in bidirectional cases.

### Distributed transactions

These are more complicated because they involve multiple Raft records and the transaction status table.  Simplifying somewhat, each time one of these transactions makes a provisional write, a Raft entry is made on the appropriate tablet and after the transaction commits, a Raft entry is made on all the involved tablets to _apply the transaction_. Applying a transaction here means converting its writes from provisional writes to regular writes.

Provisional writes are handled similarly to the normal writes in the single-shard transaction case but are written as provisional records instead of normal writes.  A special inert format is used that differs from the usual provisional records format.  This both saves space as the original locking information, which is not needed on the target side, is omitted and prevents the provisional records from interacting with the target read or locking pathways.  This ensures the transaction will not affect transactions on the target side yet.

The apply Raft entries also generate changes received by the pollers.  When a poller receives an apply entry, it sends instructions to all the target tablets it handles to apply the given transaction.  Transaction application on the target tablets is similar to that on the source universe but differs among other things due to the different provisional record format.  It converts the provisional writes into regular writes, again at the same commit time as on the source universe and with them being marked as external.  At this point the writes of the transaction to this tablet become visible to reads.

Because pollers operate independently and the writes/applies to multiple tablets are not done as a set atomically, writes from a single transaction &mdash; even a single-shard one &mdash; to multiple tablets can become visible at different times.

When a source transaction commits, it is applied to the relevant tablets lazily.  This means that even though transaction _X_ commits before transaction _Y_, _X_'s application Raft entry may occur after _Y_'s application Raft entry on some tablets.  If this happens, the writes from _X_ can become visible in the target universe after _Y_'s.  This is why non-transactional&ndash;mode reads are only eventually consistent and not timeline consistent.

### Transactional mode

xCluster safe time is computed for each database by the target-universe master leader as the minimum _xCluster application time_ any tablet in that database has reached.  Pollers determine this time using information from the source tablet servers of the form "once you have fully applied all the changes before this one, your xCluster application time for this tablet will be _T_".

A source tablet server sends such information when it determines that no active transaction involving that tablet can commit before _T_ and that all transactions involving that tablet that committed before _T_ have application Raft entries that have been previously sent as changes.  It also periodically (currently 250 ms) checks for committed transactions that are missing apply Raft entries and generates such entries for them; this helps xCluster safe time advance faster.

## Schema differences

xCluster replication does not support replicating between two copies of a table with different schemas.  For example, you cannot replicate a table to a version of that table missing a column or with a column having a different type.

More subtly, this restriction extends to hidden schema metadata like the assignment of column IDs to columns.  Just because two tables show the same schema in YSQL does not mean their schemas are actually identical.  Because of this, in practice the target table schema needs to be copied from that of the source table; see [replication bootstrapping](#replication-bootstrapping) for how this is done.

Because of this restriction, xCluster does not need to do a deep translation of row contents (for example, dropping columns or translating column IDs inside of keys and values) as rows are replicated between universes.  Avoiding deep translation simplifies the code and reduces the cost of replication.

### Supporting schema changes

Currently, this is a manual process where the exact same schema change must be manually made on first one side then the other.  Replication of the given table automatically pauses while schema differences are detected and resumes after the schemas are the same again.

Ongoing work, [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537), will make this automatic: schema changes made on the source universe will automatically be replicated to the target universe and made, allowing replication to continue running without operator intervention.


## Replication bootstrapping

xCluster replication copies changes made on the source universe to the target universe.  This is fine if the source universe starts empty but what if we want to start replicating a universe that already contains data?

In that case, we need to bootstrap the replication process by first copying the source universe to the target universe.

Today, this is done by backing up the source universe and restoring it to the target universe.  In addition to copying all the data, this copies the table schemas so they are identical on both sides.  Before the backup is done, the current Raft log IDs are saved so the replication can be started after the restore from a time before the backup was done.  This ensures any data written to the source universe during the backup is replicated.

Ongoing work, [#17862](https://github.com/yugabyte/yugabyte-db/issues/17862), will replace using backup and restore here with directly copying RocksDB files between the source and target universes.  This will be more performant and flexible and remove the need for external storage like S3 to set up replication.

## Deployment scenarios

xCluster currently supports active-active single-master and active-active multi-master deployments.

### Active-active single-master

In this setup, replication is unidirectional from a source universe to a target universe, typically located in different data centers or regions. The source universe can handle both reads and writes, while the target universe is read-only. Because only the source universe can accept writes, this mode is referred to as single-master. Note that in the source universe, all nodes can serve writes.

These deployments are typically used for serving low-latency reads from the target universes and for disaster recovery purposes. When the primary purpose is disaster recovery, these deployments are referred to as active-standby, as the target universe is on standby to take over if the source universe fails.

Transactional mode is generally preferred for this deployment because it ensures consistency even if the source universe is lost. However, non-transactional mode can also be used depending on the specific requirements and trade-offs.

{{<lead link="https://youtu.be/6rmrcVQqb0o?si=4CuiByQGLaNzhdn_">}}
To learn more, watch [Disaster Recovery in YugabyteDB](https://youtu.be/6rmrcVQqb0o?si=4CuiByQGLaNzhdn_)
{{</lead>}}

The following diagram shows an example of this deployment:

![example of active-passive deployment](/images/architecture/replication/active-standby-deployment-new.png)

### Active-active multi-master

In a multi-master deployment, data replication is bidirectional between two universes, allowing both universes to perform reads and writes. Writes to any universe are asynchronously replicated to the other universe with a timestamp for the update. This mode implements last-writer-wins, where if the same key is updated in both universes around the same time, the write with the larger timestamp overrides the other one. This deployment mode is called multi-master because both universes serve writes.

The multi-master deployment uses bidirectional replication, which involves two unidirectional replication streams operating in non-transactional mode. Special measures are taken to assign timestamps that ensure last-writer-wins semantics, and data received from the replication stream is not re-replicated.

The following diagram illustrates this deployment:

![example of active-active deployment](/images/architecture/replication/active-active-deployment-new.png)

### Unsupported deployment scenarios

The following deployment scenarios are not yet supported:

- _Broadcast_: This topology involves one source universe sending data to many target universes, for example: `A -> B, A -> C`. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

- _Consolidation_: This topology involves many source universes sending data to one central target universe, for example: `B -> A, C -> A`. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

- _Daisy chaining_: This involves connecting a series of universes, for example: `A -> B -> C`

- _Star_: This involves connecting all universes to each other, for example: `A <-> B <-> C <-> A`


## Limitations

There are a number of limitations in the current xCluster implementation besides what deployments are possible.

### Database triggers do not fire for replicated data

Because xCluster replication bypasses the query layer, any database triggers are not fired on the target side for replicated records, which can result in unexpected behavior.

### Constraints cannot be enforced in active-active multi-master

Similarly, there is no way to check for violations of unique constraints in active-active multiple-master setups. It is possible, for example, to have two conflicting writes in separate universes that together would violate a unique constraint and cause the main table to contain both rows, yet the index to contain only one row, resulting in an inconsistent state.

Because of this applications using active-active multi-master should avoid `UNIQUE` indexes and constraints as well as serial columns in primary keys: Because both universes generate the same sequence numbers, this can result in conflicting rows. It is recommended to use UUIDs instead.

In the future, it may be possible to detect such unsafe constraints and issue a warning, potentially by default.  This is tracked in [#11539](https://github.com/yugabyte/yugabyte-db/issues/11539).

Note that if you attempt to insert the same row on both universes at the same time to a table that does not have a primary key then you will end up with two rows with the same data. This is the expected PostgreSQL behavior &mdash; tables without primary keys can have multiple rows with the same data.

### Materialized views

[Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) are not replicated by xCluster. When setting up replication for a database, materialized views need to be excluded. You can create them on the target universe after the replication is set up. When refreshing, make sure to refresh on both sides.

### Non-transactional

When interacting with data replicated from another universe using non-transactional mode:

- Reads are only eventually consistent
- Last writer wins for writes
- Transactions are limited to isolation level SQL-92 READ COMMITTED

After losing one universe, the other universe may be left with torn transactions.

### Transactional

Transactional mode has the following limitations:

- By default, no writes are allowed in the target universe.

  In v2024.2.3 and later, you can allow writes to the target on an exception basis, overriding the default read-only behavior by setting the following YSQL configuration parameter before executing a DML operation:

  ```sql
  SET yb_non_ddl_txn_for_sys_tables_allowed = true
  ```

  This is intended strictly for specialized use cases, such as enabling tools like Flyway to update maintenance tables (for example, schema version trackers) on the replica.

  {{< warning title="Important" >}}
Improper use can compromise replication consistency and lead to data divergence. Use this setting only when absolutely necessary and with a clear understanding of its implications.
  {{< /warning >}}

- Active-active multi-master is not supported
- YCQL is not yet supported

When the source universe is lost, an explicit decision must be made to switch over to the standby universe and point-in-time recovery must run; this is expected to increase recovery time by a minute or so.

### Bootstrapping replication

- Currently, it is your responsibility to ensure that a target universe has sufficiently recent updates so that replication can safely resume (for instructions, refer to [Bootstrap a target universe](../../../deploy/multi-dc/async-replication/async-deployment/#bootstrap-a-target-universe)). In the future, bootstrapping the target universe will be automated, which is tracked in [#11538](https://github.com/yugabyte/yugabyte-db/issues/11538).
- Bootstrap currently relies on the underlying backup and restore (BAR) mechanism of YugabyteDB.  This means it also inherits all of the limitations of BAR.  For YSQL, currently the scope of BAR is at a database level, while the scope of replication is at table level.  This implies that when you bootstrap a target universe, you automatically bring any tables from the source database to the target database, even the ones that you might not plan to actually configure replication on.  This is tracked in [#11536](https://github.com/yugabyte/yugabyte-db/issues/11536).

### DDL changes

- Currently, DDL changes are not automatically replicated.  Applying commands such as `CREATE TABLE`, `ALTER TABLE`, and `CREATE INDEX` to the target universes is your responsibility.
- `DROP TABLE` is not supported in YCQL.  You must first disable replication for this table.
- `TRUNCATE TABLE` is not supported.  This is an underlying limitation, due to the level at which the two features operate.  That is, replication is implemented on top of the Raft WAL files, while truncate is implemented on top of the RocksDB SST files.
- In the future, it will be possible to propagate DDL changes safely to other universes.  This is tracked in [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537).

### Kubernetes

- xCluster replication can be set up with Kubernetes-deployed universes. However, the source and target must be able to communicate by directly referencing the pods in the other universe. In practice, this either means that the two universes must be part of the same Kubernetes cluster or that two Kubernetes clusters must have DNS and routing properly set up amongst themselves.
- Having two YugabyteDB universes, each in their own standalone Kubernetes cluster, communicating with each other via a load balancer, is not currently supported. See [#2422](https://github.com/yugabyte/yugabyte-db/issues/2422) for details.

### Backups

Backups are supported. However for backups on target clusters, if there is an active workload, consistency of the latest data is not guaranteed.

## Cross-feature interactions

A number of interactions across features are supported.

### Supported

- TLS is supported for both client and internal RPC traffic.  Universes can also be configured with different certificates.
- RPC compression is supported.  Note that both universes must be on a version that supports compression before a compression algorithm is enabled.
- Encryption at rest is supported.  Note that the universes can technically use different Key Management Service (KMS) configurations.  However, for bootstrapping a target universe, the reliance is on the backup and restore flow.  As such, a limitation from that is inherited, which requires that the universe being restored has at least access to the same KMS as the one in which the backup was taken.  This means both the source and the target must have access to the same KMS configurations.
- YSQL colocation is supported.
- YSQL geo-partitioning is supported.  Note that you must configure replication on all new partitions manually as DDL changes are not replicated automatically.
- Source and target universes can have different numbers of tablets.
- Tablet splitting is supported on both source and target universes.

{{< tip title="Explore" >}}
To better understand how xCluster replication works in practice, see [xCluster deployment](../../../deploy/multi-dc/async-replication/) and [Transactional xCluster deployment](../../../deploy/multi-dc/async-replication/async-replication-transactional/) in Launch and Manage.
{{< /tip >}}
