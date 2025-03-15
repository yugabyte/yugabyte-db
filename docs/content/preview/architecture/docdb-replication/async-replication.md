---
title: xCluster
headerTitle: xCluster replication
linkTitle: xCluster
description: xCluster replication between multiple YugabyteDB universes.
headContent: High throughput asynchronous replication between independent YugabyteDB universes
aliases:
  - /preview/architecture/docdb/2dc-deployments/
menu:
  preview:
    identifier: architecture-docdb-async-replication
    parent: architecture-docdb-replication
    weight: 300
type: docs
---

{{< figure src="/images/architecture/xCluster-icon.png" title="" class="w-50">}}

## Synchronous versus asynchronous replication

YugabyteDB's [synchronous replication](../replication/) can be used to tolerate losing entire data centers or regions.  It replicates data in a single universe spread across multiple (three or more) data centers so that the loss of one data center does not impact availability, durability, or strong consistency enabled by the Raft consensus algorithm.

However, asynchronous replication can be beneficial in certain scenarios:

- _Low write latency_: With synchronous replication, each write must reach a consensus across a majority of data centers. This can add tens or even hundreds of milliseconds of extra latency for writes in a multi-region deployment. xCluster reduces this latency by eliminating the need for immediate consensus across regions.
- _Only two data centers needed_: With synchronous replication, to tolerate the failure of `f` fault domains, you need at least `2f + 1` fault domains. Therefore, to survive the loss of one data center, a minimum of three data centers is required, which can increase operational costs. For more details, see [fault tolerance](../replication/#fault-tolerance). With xCluster you can achieve multi-region deployments with only two data centers.
- _Disaster recovery_: xCluster utilizes independent YugabyteDB universes in each region that can function independently of each other. This setup allows for quick failover with minimal data loss in the event of a regional outage caused by hardware or software issues.

The drawback of Asynchronous replication is:

- __Data loss on failure__: When a data center fails, any data that has not yet been replicated will be lost. The amount of data lost depends on the replication lag, which is usually subsecond but varies based on the network characteristics between the two data centers.
- __Stale reads__: When reading from the secondary data center, there may be a delay in data availability due to the asynchronous nature of the replication. This can result in stale reads, which may not reflect the most recent writes. Non-transactional modes can serve torn reads of recently written data.


{{< tip title="Deploy" >}}
To better understand how xCluster replication works in practice, check out [xCluster deployment](../../../deploy/multi-dc/async-replication/).
{{< /tip >}}

## YugabyteDB's xCluster replication

xCluster replication is YugabyteDB's implementation of high throughput asynchronous replication between two YugabyteDB universes.  It allows you to set up one or more unidirectional replication _flows_ between universes.  Note that xCluster can only be used to replicate between primary clusters in two different universes; it cannot be used to replicate between clusters in the same universe.  (See [universe versus cluster](../../key-concepts/#universe) for more on the distinction between universes and clusters.)

For each flow, data is replicated from a _source_ (also called a producer) universe to a _target_ (also called a consumer) universe.  Replication is done at the DocDB layer, by efficiently replicating WAL records asynchronously to the target universe.  Both YSQL and YCQL are supported.

Multiple flows can be used; for example, two unidirectional flows between two universes, one in each direction, produce bidirectional replication where anything written in one universe will be replicated to the other &mdash; data is replicated only once to avoid infinite loops.  See [supported deployment scenarios](#supported-deployment-scenarios) for which flow combinations are currently supported.

Although for simplicity, we will describe flows between entire universes, flows are actually composed of streams between pairs of tables, one in each universe, allowing replication of only certain namespaces or tables.

{{< tip >}}
To understand the difference between xCluster, Geo-Partitioning and Read Replicas, refer to [Multi-Region Deployments](../../../explore/multi-region-deployments/).
{{< /tip >}}


## Asynchronous replication modes

Because there is a useful trade-off between how much consistency is lost and what transactions are allowed, YugabyteDB provides two different modes of asynchronous replication:

- __Non-transactional replication__: writes are allowed on the target-universe, but reads of recently replicated data can be inconsistent.
- __Transactional replication__: consistency of reads is preserved on the target-universe, but writes are not allowed.


### Non-transactional replication

All writes to the source universe are independently replicated to the target universe where they are applied with the same timestamp they committed on the source universe.  No locks are taken or honored on the target side.

Due to replication lag, a read performed in the target universe immediately after a write in the source universe may not reflect the recent write. In other words, reads in the target universe do not wait for the latest data from the source universe to become available.

Note that the writes are usually being written in the past as far as the target universe is concerned.  This violates the preconditions for YugabyteDB serving consistent reads (see the discussion on [safe timestamps](../../transactions/single-row-transactions/#safe-timestamp-assignment-for-a-read-request)).  Accordingly, reads on the target universe are no longer strongly consistent but rather eventually consistent even within a single table.

If both target and source universes write to the same key then the last writer wins.  The deciding factor is the underlying hybrid time of the updates from each universe.

#### Inconsistencies affecting transactions

Because the writes are being independently replicated, a transaction from the source universe becomes visible over time.  This means transactions in the target universe can see non-repeated reads and phantom reads no matter what their declared isolation level is.  Effectively then all transactions on the target universe are at SQL-92 isolation level READ COMMITTED, which only guarantees that transactions never read uncommitted data.  Unlike the normal YugabyteDB READ COMMITTED level, this does not guarantee a statement will see a consistent snapshot or all the data that has been committed before the statement is issued.

If the source universe fails, then the target universe may be left in an inconsistent state where some source universe transactions have only some of their writes applied in the target universe (these are called _torn transactions_).  This inconsistency will not automatically heal over time and may need to be manually resolved.

Note that these inconsistencies are limited to the tables/rows being written to and replicated from the source universe: any target transaction that does not interact with such rows is unaffected.
{{< tip >}}
For YSQL deployments, transactional mode is preferred because it provides the necessary consistency guarantees typically required for such deployments.
{{< /tip >}}

### Transactional replication

In this mode, reads occur at a time sufficiently in the past (typically 1-2 seconds) to ensure that all relevant data from the source universe has already been replicated. Additionally, writes to the target universe are not allowed.

Reads occur as of the _xCluster safe time_, ensuring that all writes from all source transactions that will commit at or before the _xCluster safe time_ have been replicated to the target universe. This means we read as of a time far enough in the past that there cannot be new incoming commits at or before that time. This guarantees consistent reads and ensures source universe transactions become visible atomically. Note that the _xCluster safe time_ is not blocked by any in-flight or long-running source-universe transactions.

_xCluster safe time_ advances as replication proceeds but lags behind real-time by the current replication lag.  This means, for example, if we write at 2:00:00 PM in the source universe and read at 2:00:01 PM in the target universe and replication lag is say five seconds then the read may read as of 1:59:56 PM and will not see the write.  We may not be able to see the write until 2:00:06 PM in the target universe assuming the replication lag remains at five seconds.

If the source universe fails, we can discard all incomplete information in the target universe by rewinding it to the latest _xCluster safe time_ (1:59:56 PM in the example) using YugabyteDB's [Point-in-Time Recovery (PITR)](../../../manage/backup-restore/point-in-time-recovery/) feature. The result will be a consistent database that includes only the transactions from the source universe that committed at or before the _xCluster safe time_. Unlike with non-transactional replication, there is no need to handle torn transactions.

Target universe read-only transactions run at serializable isolation level on a single consistent snapshot as of the _xCluster safe time_.

In xCluster transactional replication mode, writes to the target universe are not allowed. Consequently, this mode does not support bidirectional replication.

Transactional replication is currently only available for YSQL deployments.

Transactional replication intern comes in three modes:

#### Automatic mode
 {{<tags/feature/tp>}}

In this mode all aspects of replication are handled automatically, including schema changes.

#### Semi-automatic mode
Provides operationally simpler setup and management of replication, as well as fewer steps for performing DDL changes. This is the recommended mode for new deployments.

#### Manual mode
This mode is deprecated and not recommended for new deployments. It requires manual intervention for schema changes and is more complex to set up and manage.

{{<lead link="https://youtu.be/lI6gw7ncBs8?si=gAioZ_NgOyl2dsM5">}}
To learn more, watch [Transactional xCluster](https://youtu.be/lI6gw7ncBs8?si=gAioZ_NgOyl2dsM5)
{{</lead>}}


## High-level implementation details

At a high level, xCluster replication is implemented by having _pollers_ in the target universe that poll the source universe tablet servers for WAL records.  Each poller works independently and polls one source tablet, distributing the received changes among one or more target tablets.
The polled tablets examine only the WAL to determine recent changes rather than looking at their RocksDB instances. The incoming poll request specifies the WAL OpId to start gathering changes from, and the response includes a batch of changes and the WAL OpId to continue with next time.

The source universe periodically saves the OpId that the target universe has confirmed as processed. This information is stored in the `cdc_state` table.

{{<lead link="https://youtu.be/9TF3xPDDJ30?si=foKnj1CvDYidHqmx">}}
To learn more, watch [xCluster Replication](https://youtu.be/9TF3xPDDJ30?si=foKnj1CvDYidHqmx)
{{</lead>}}

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
{{< tip >}}
This section does not apply to Automatic mode since it replicates the schema changes.
{{< /tip >}}

xCluster replication requires that the source and target tables have identical schemas. This means that you cannot replicate data between tables if there are differences in their schemas, such as missing columns or columns with different data types. Ensuring schema consistency is crucial for the replication process to function correctly.

Additionally, this restriction includes hidden schema metadata, such as the assignment of column IDs. Even if two tables appear to have the same schema in YSQL, their schemas might not be identical. Therefore, in practice, the target table schema should be copied from the source table schema. For more details, refer to [replication bootstrapping](#replication-bootstrapping).

Because of this restriction, xCluster does not need to perform deep translations of row contents (such as dropping columns or translating column IDs within keys and values) when replicating rows between universes. This avoidance of deep translation reduces the replication cost and improves throughput.

Schema changes must be manually applied first to the source universe and then to the target universe. During this process, replication for the affected table is automatically paused when schema differences are detected and resumes once the schemas are identical.

## Replication bootstrapping

xCluster replicates the source WAL records to the target universe.  WAL is garbage collected over time to conserve disk space. When setting up a new replication flow, the source universe may have already deleted some of the WAL records that are needed for an empty target universe to catch up. This is especially likely if the source universe has been running for a while and has accumulated a lot of WAL.

In this case, we need to bootstrap the target universe.

This process involves checkpointing the source universe to ensure that any new WAL records are preserved for xCluster. Following this, a [distributed backup](../../../manage/backup-restore/snapshot-ysql/#move-a-snapshot-to-external-storage) is performed and restored to the target universe. This not only copies all the data but also ensures that the table schemas are identical on both sides.

## Supported deployment scenarios

xCluster currently supports active-active single-master and active-active multi-master deployments.

### Active-active single-master

In this setup the replication is unidirectional from a source universe to a target universe. The target universe is typically located in data centers or regions that are different from the source universe. The source universe can serve both reads and writes. The target universe can only serve reads. Since only the nodes in one universe can take writes this mode is referred to as single master. Note that within the source universe all nodes can serve writes.

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

### Materialized views are not supported

Setting up xCluster replication for [materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) is currently not supported. When setting up replication for a database, materialized views need to be excluded. YugabyteDB Anywhere automatically excludes materialized views from replication setup.

### Non-transactional&ndash;mode consistency issues

When interacting with data replicated from another universe using non-transactional mode:

- Reads are only eventually consistent
- Last writer wins for writes
- Transactions are limited to isolation level SQL-92 READ COMMITTED

After losing one universe, the other universe may be left with torn transactions.

### Transactional-mode limitations

Transactional mode has the following limitations:

- No writes are allowed in the target universe
- Active-active multi-master is not supported
- YCQL is not yet supported

When the source universe is lost, an explicit decision must be made to switch over to the standby universe and point-in-time recovery must run; this is expected to increase recovery time by a minute or so.


### Backups

Backups are supported. However for backups on target clusters, if there is an active workload, consistency of the latest data is not guaranteed.
