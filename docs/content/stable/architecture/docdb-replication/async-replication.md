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
    weight: 1150
type: docs
---

## Synchronous versus asynchronous replication

YugabyteDB's [synchronous replication](../replication/) can be used to tolerate losing entire data centers or regions.  It replicates data within a single universe spread across multiple (three or more) data centers so that the loss of one data center does not impact availability, durability, or strong consistency courtesy of the Raft consensus algorithm.

However, synchronous replication has two important drawbacks when used this way:

- __High write latency__: each write must achieve consensus across at least two data centers, which means at least one round trip between data centers.  This can add tens or even hundreds of milliseconds of extra latency in a multi-region deployment.

- __Need for at least three data centers__: because consensus requires an odd number of fault domains so ties can be broken, at least three data centers must be used, which adds operational cost.

As an alternative, YugabyteDB provides asynchronous replication that replicates data between two or more separate universes.  It does not suffer from the drawbacks of synchronous replication: because it is done in the background, it does not impact write latency, and because it does not use consensus it does not require a third data center.

Asynchronous replication has its own drawbacks, however, including:

- __Data loss on failure__: when a universe fails, the data in it that has not yet been replicated will be lost.  The amount of data lost depends on the replication lag, which is usually subsecond.

- __Limitations on transactionality__: Because transactions in the universes cannot coordinate with each other, either the kinds of transactions must be restricted or some consistency and isolation must be lost.


## YugabyteDB's xCluster replication

xCluster replication is YugabyteDB's implementation of asynchronous replication for disaster recovery.  It allows you to set up one or more unidirectional replication _flows_ between universes.  Note that xCluster can only be used to replicate between primary clusters in two different universes; it cannot be used to replicate between clusters in the same universe.  (See [universe versus cluster](../../concepts/universe#universe-vs-cluster) for more on the distinction between universes and clusters.)

For each flow, data is replicated from a _source_ (also called a producer) universe to a _target_ (also called a consumer) universe.  Replication is done at the DocDB level, with newly committed writes in the source universe asynchronously replicated to the target universe.  Both YSQL and YCQL are supported.

Multiple flows can be used; for example, two unidirectional flows between two universes, one in each direction, produce bidirectional replication where anything written in one universe will be replicated to the other &mdash; data is only asynchronously replicated once to avoid infinite loops.  See [supported deployment scenarios](#supported-deployment-scenarios) for which flow combinations are currently supported.

Although for simplicity, we will describe flows between entire universes, flows are actually composed of streams between pairs of tables, one in each universe, allowing replication of only certain namespaces or tables.

xCluster is more flexible than a hypothetical scheme whereby read replicas are promoted to full replicas when primary replicas are lost because it does not require the two universes to be tightly coupled. With xCluster, for example, the same table can be split into tablets in different ways in the two universes. xCluster also allows for bidirectional replication, which is not possible using read replicas because read replicas cannot take writes.


## Asynchronous replication modes

Because there is a useful trade-off between how much consistency is lost and what transactions are allowed, YugabyteDB provides two different modes of asynchronous replication:

- __non-transactional replication__: all transactions are allowed but some consistency is lost
- __transactional replication__: consistency is preserved but target-universe transactions must be read-only

### Non-transactional replication

Here, after each transaction commits in the source universe, its writes are independently replicated to the target universe where they are applied with the same timestamp they had on the source universe.  No locks are taken or honored on the target side.

Note that the writes are usually being written in the past as far as the target universe is concerned.  This violates the preconditions for YugabyteDB serving consistent reads (see the discussion on [safe timestamps](../../transactions/single-row-transactions/#safe-timestamp-assignment-for-a-read-request)).  Accordingly, reads on the target universe are no longer strongly consistent but rather eventually consistent.

If both target and source universes write to the same key then the last writer wins.  The deciding factor is the underlying hybrid time of the updates from each universe.

Because of replication lag, a read done immediately in the target universe after a write done on the source universe may not see that write.  Another way of putting this is that reads in the target universe do not wait for up-to-date data from the source universe to become visible.

#### Inconsistencies affecting transactions

Because the writes are being independently replicated, a transaction from the source universe becomes visible over time.  This means transactions in the target universe can see non-repeated reads and phantom reads no matter what their declared isolation level is.  Effectively then all transactions on the target universe are at SQL-92 isolation level READ COMMITTED, which only guarantees that transactions never read uncommitted data.  Unlike the normal YugabyteDB READ COMMITTED level, this does not guarantee a statement will see a consistent snapshot or all the data that has been committed before the statement is issued.

If the source universe dies, then the target universe may be left in an inconsistent state where some source universe transactions have only some of their writes applied in the target universe (these are called _torn transactions_).  This inconsistency will not automatically heal over time and may need to be manually resolved.

Note that these inconsistencies are limited to the tables/rows being written to and replicated from the source universe: any target transaction that does not interact with such rows is unaffected.

### Transactional replication

This mode is an extension of the previous one.  In order to restore consistency, we additionally disallow writes on the target universe and cause reads to read as of a time far enough in the past (typically 250 ms) that all the relevant data from the source universe has already been replicated.

In particular, we pick the time to read as of, _T_, so that all the writes from all the source transactions that will commit at or before time _T_ have been replicated to the target universe.  Put another way, we read as of a time far enough in the past that there cannot be new incoming source commits at or before that time.  This restores consistent reads and ensures source universe transaction results become visible atomically.  Note that we do *not* wait for any current in flight source-universe transactions.

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

In simple cases, we can associate a poller with each target tablet that polls the corresponding source tablet.

However, in the general case the number of tablets for a table in the source universe and in the target universe may be different.  Even if the number of tablets is the same, they may have different sharding boundaries due to tablet splits occurring at different places in the past.

This means that each target tablet may need the changes from multiple source tablets and multiple target tablets may need changes from the same source tablet.  To avoid multiple redundant cross-universe reads to the same source tablet, only one poller reads from each source tablet; in cases where a source tablet's changes are needed by multiple target tablets, the poller assigned to that source tablet distributes the changes to the relevant target tablets.

The following illustration shows what this might look like for one table:

![distribution of pollers and where they pull data from and send it to](/images/architecture/replication/distribution-of-pollers-new.png)

Here, the source universe is on the left with three TServers (the white boxes) each containing one tablet of the table (the boxes inside) with the shown ranges of the table.  The target universe is on the right with one fewer TServer and tablet.  As you can see, the top source tablet's data is split among both target tablets by the poller running in the top target TServer and the remaining source tablets' data is replicated to the second target tablet by the pollers running in the other target TServer.  For simplicity, only the tablet leaders are shown here &mdash; pollers run at and poll from only leaders.

Tablet splitting generates a Raft log entry, which is replicated to the target side so that the mapping of pollers to source tablets can be updated as needed when a source tablet splits.

### Single-shard transactions

These are straightforward: when one of these transaction commits, a single Raft log entry is produced containing all of that transaction's writes and its commit time.  This entry in turn is used to generate part of a batch of changes when the poller requests changes.

Upon receiving the changes, the poller examines each write to see what key it writes to in order to determine which target tablet covers that part of the table.  The poller then forwards the writes to the appropriate tablets. The commit times of the writes are preserved and the writes are marked as _external_, which prevents them from being further replicated by xCluster, whether onwards to an additional cluster or back to the cluster they came from in bidirectional cases.

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

Because of this restriction, xCluster does not need to do a deep translation of row contents (e.g., dropping columns or translating column IDs inside of keys and values) as rows are replicated between universes.  Avoiding deep translation simplifies the code and reduces the cost of replication.

### Supporting schema changes

Today, this is a manual process where the exact same schema change must be manually made on first one side then the other.  Replication of the given table automatically pauses while schema differences are detected and resumes once the schemas are the same again.

Ongoing work, [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537), will make this automatic: schema changes made on the source universe will automatically be replicated to the target universe and made, allowing replication to continue running without operator intervention.


## Replication bootstrapping

xCluster replication copies changes made on the source universe to the target universe.  This is fine if the source universe starts empty but what if we want to start replicating a universe that already contains data?

In that case, we need to bootstrap the replication process by first copying the source universe to the target universe.

Today, this is done by backing up the source universe and restoring it to the target universe.  In addition to copying all the data, this copies the table schemas so they are identical on both sides.  Before the backup is done, the current Raft log IDs are saved so the replication can be started after the restore from a time before the backup was done.  This ensures any data written to the source universe during the backup is replicated.

Ongoing work, [#17862](https://github.com/yugabyte/yugabyte-db/issues/17862), will replace using backup and restore here with directly copying RocksDB files between the source and target universes.  This will be more performant and flexible and remove the need for external storage like S3 to set up replication.


## Supported deployment scenarios

xCluster currently supports active-active single-master and active-active multi-master deployments.

### Active- active single-master

Here the replication is unidirectional from a source universe to a target universe. The target universe is typically located in data centers or regions that are different from the source universe. The source universe can serve both reads and writes. The target universe can only serve reads. Since only the nodes in one universe can take writes this mode is referred to as single master. Note that within the source universe all nodes can serve writes.

Usually, such deployments are used for serving low-latency reads from the target universes, as well as for disaster recovery purposes.  When used primarily for disaster recovery purposes, these deployments are also called active-standby because the target universe stands by to take over if the source universe is lost.

Either transactional or non-transactional mode can be used here, but transactional mode is usually preferred because it provides consistency if the source universe is lost.

The following diagram shows an example of this deployment:

![example of active-passive deployment](/images/architecture/replication/active-standby-deployment-new.png)

### Active-active multi-master

The replication of data can be bidirectional between two universes, in which case both universes can perform reads and writes. Writes to any universe are asynchronously replicated to the other universe with a timestamp for the update. If the same key is updated in both universes at similar times, this results in the write with the larger timestamp becoming the latest write. In this case, both the universes serve writes, hence this deployment mode is called multi-master.

The multi-master deployment is built using bidirectional replication which has two unidirectional replication streams using non-transactional mode. Special care is taken to ensure that the timestamps are assigned to guarantee last-writer-wins semantics and the data arriving from the replication stream is not re-replicated.

The following diagram shows an example of this deployment:

![example of active-active deployment](/images/architecture/replication/active-active-deployment-new.png)

## Not supported deployment scenarios

A number of deployment scenarios are not yet supported in YugabyteDB.

### Broadcast

This topology involves one source universe sending data to many target universes. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

### Consolidation

This topology involves many source universes sending data to one central target universe. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

### More complex topologies

Outside of the traditional 1:1 topology and the previously described 1:N and N:1 topologies, there are many other desired configurations that are not currently supported, such as the following:

- Daisy chaining, which involves connecting a series of universes as both source and target, for example: `A <-> B <-> C`
- Ring, which involves connecting a series of universes in a loop, for example: `A <-> B <-> C <-> A`

Some of these topologies might become naturally available as soon as the [Broadcast](#broadcast) and [Consolidation](#consolidation) use cases are resolved, thus allowing a universe to simultaneously be both a source and a target to several other universes. For details, see [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535).


## Limitations

There are a number of limitations in the current xCluster implementation besides what deployments are possible.

### Database triggers do not fire for replicated data

Because xCluster replication bypasses the query layer, any database triggers are not fired on the target side for replicated records, which can result in unexpected behavior.

### Constraints cannot be enforced in active-active multi-master

Similarly, there is no way to check for violations of unique constraints in active-active multiple-master setups. It is possible, for example, to have two conflicting writes in separate universes that together would violate a unique constraint and cause the main table to contain both rows, yet the index to contain only one row, resulting in an inconsistent state.

Because of this applications using active-active multi-master should avoid `UNIQUE` indexes and constraints as well as serial columns in primary keys: Because both universes generate the same sequence numbers, this can result in conflicting rows. It is recommended to use UUIDs instead.

In the future, it may be possible to detect such unsafe constraints and issue a warning, potentially by default.  This is tracked in [#11539](https://github.com/yugabyte/yugabyte-db/issues/11539).

Note that if you attempt to insert the same row on both universes at the same time to a table that does not have a primary key then you will end up with two rows with the same data. This is the expected PostgreSQL behavior &mdash; tables without primary keys can have multiple rows with the same data.

### Materialized views are not supported

Setting up xCluster replication for [materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) is currently not supported. When setting up replication for a database, materialized views need to be excluded. YugabyteDB Anywhere automatically excludes materialized views from replication setup.

### Non-transactional&ndash;mode consistency issues

When interacting with data replicated from another universe using non-transactional mode:

- Reads are only eventually consistent
- Last writer wins for writes
- Transactions are limited to isolation level SQL-92 READ COMMITTED

After losing one universe, the other universe may be left with torn transactions.

### Transactional-mode limitations

With transactional mode,

- No writes are allowed in the target universe
- Active-active multi-master is not supported
- YCQL is not yet supported

When the source universe is lost, an explicit decision must be made to switch over to the standby universe and point-in-time recovery must run; this is expected to increase recovery time by a minute or so.

### Bootstrapping replication

- Currently, it is your responsibility to ensure that a target universe has sufficiently recent updates so that replication can safely resume (for instructions, refer to [Bootstrap a target universe](../../../deploy/multi-dc/async-replication/async-deployment/#bootstrap-a-target-universe)). In the future, bootstrapping the target universe will be automated, which is tracked in [#11538](https://github.com/yugabyte/yugabyte-db/issues/11538).
- Bootstrap currently relies on the underlying backup and restore (BAR) mechanism of YugabyteDB.  This means it also inherits all of the limitations of BAR.  For YSQL, currently the scope of BAR is at a database level, while the scope of replication is at table level.  This implies that when you bootstrap a target universe, you automatically bring any tables from the source database to the target database, even the ones that you might not plan to actually configure replication on.  This is tracked in [#11536](https://github.com/yugabyte/yugabyte-db/issues/11536).

### DDL changes

- Currently, DDL changes are not automatically replicated.  Applying commands such as `CREATE TABLE`, `ALTER TABLE`, and `CREATE INDEX` to the target universes is your responsibility.
- `DROP TABLE` is not supported.  You must first disable replication for this table.
- `TRUNCATE TABLE` is not supported.  This is an underlying limitation, due to the level at which the two features operate.  That is, replication is implemented on top of the Raft WAL files, while truncate is implemented on top of the RocksDB SST files.
- In the future, it will be possible to propagate DDL changes safely to other universes.  This is tracked in [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537).

### Kubernetes

- Technically, xCluster replication can be set up with Kubernetes-deployed universes.  However, the source and target must be able to communicate by directly referencing the pods in the other universe.  In practice, this either means that the two universes must be part of the same Kubernetes cluster or that two Kubernetes clusters must have DNS and routing properly set up amongst themselves.
- Being able to have two YugabyteDB universes, each in their own standalone Kubernetes cluster, communicating with each other via a load balancer, is not currently supported, as per [#2422](https://github.com/yugabyte/yugabyte-db/issues/2422).

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

{{< note title="Note" >}}

To better understand how xCluster replication works in practice, see [xCluster deployment](../../../deploy/multi-dc/async-replication/) and [Transactional xCluster deployment](../../../deploy/multi-dc/async-replication-transactional/) in Launch and Manage.

{{< /note >}}
