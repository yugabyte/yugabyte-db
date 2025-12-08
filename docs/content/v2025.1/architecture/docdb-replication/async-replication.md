---
title: xCluster
headerTitle: xCluster replication
linkTitle: xCluster
description: xCluster replication between multiple YugabyteDB universes.
headContent: High-throughput asynchronous physical replication
menu:
  v2025.1:
    identifier: architecture-docdb-async-replication
    parent: architecture-docdb-replication
    weight: 300
rightNav:
  hideH4: true
type: docs
---

{{< figure src="/images/architecture/xCluster-icon.png" title="" class="w-50">}}

## YugabyteDB's xCluster replication

xCluster replication is YugabyteDB's implementation of high-throughput asynchronous physical replication between two YugabyteDB universes. It allows you to set up one or more unidirectional replication _flows_ between universes. For each flow, data is replicated from a _source_ (also called a producer) universe to a _target_ (also called a consumer) universe. Replication is done at the DocDB layer, by efficiently replicating WAL records asynchronously to the target universe. Both YSQL and YCQL are supported.

Multiple flows can be configured; for instance, setting up two unidirectional flows between two universes, one in each direction, enables bidirectional replication. This ensures that data written in one universe is replicated to the other without causing infinite loops. Refer to [supported deployment scenarios](#deployment-scenarios) for details on the supported flow combinations.

For simplicity, flows are described as being between entire universes. However, flows are actually composed of streams between pairs of YCQL tables or YSQL databases, one in each universe, allowing replication of only certain tables or databases.

Note that xCluster can only be used to replicate between primary clusters in two different universes; it cannot be used to replicate between clusters in the same universe. (See [universe versus cluster](../../key-concepts/#universe) for more on the distinction between universes and clusters.)

{{< tip >}}
To understand the difference between xCluster, Geo-Partitioning, and Read Replicas, refer to [Multi-Region Deployments](../../../explore/multi-region-deployments/).
{{< /tip >}}

## Synchronous versus asynchronous replication

YugabyteDB's [synchronous replication](../replication/) can be used to tolerate losing entire data centers or regions. It replicates data in a single universe spread across multiple (three or more) data centers so that the loss of one data center does not impact availability, durability, or strong consistency enabled by the Raft consensus algorithm.

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

- Non-transactional replication: Writes are allowed on the target universe, but reads of recently replicated data can be inconsistent.
- Transactional replication: Consistency of reads is preserved on the target universe, but writes are not allowed.

### Non-transactional replication

For YCQL, only non-transactional replication mode is supported.
Non-transactional replication mode supports [active-active multi-master](#active-active-multi-master) deployments.

{{< Warning title="Important" >}}
Refer to [Inconsistencies affecting transactions](#inconsistencies-affecting-transactions) for details on how non-transactional mode can lead to inconsistencies.
{{< /Warning >}}

{{< tip >}}
For YSQL deployments, [transactional mode](#transactional-replication) is preferred because it provides the necessary consistency guarantees typically required for such deployments.
{{< /tip >}}

#### Inconsistencies affecting transactions

Writes to the source universe tablets are independently replicated to the target universe tablets, where they are applied with the same timestamp they committed on the source universe. No locks are taken or honored on the target side.

Due to replication lag, reads in the target universe may not immediately reflect recent writes from the source universe. When a read is performed in the target universe, it proceeds without waiting for the latest source data to become available. Additionally, because writes are replicated independently, reads don’t wait for all related writes to arrive. This applies both within a single table (across tablets) and across different tables and their indexes.

For YSQL, transactions on the target universe can experience non-repeatable reads and phantom reads, and do not honor the declared isolation level.

If both target and source universes write to the primary key row, then the last writer wins. The deciding factor is the underlying hybrid time of the updates from each universe.
Concurrent updates and deletes of the same primary key row can result in inconsistent data.

For YSQL, if there are indexes involved then the write can result in corruption of the index. Concurrent updates, and deletes to the primary key row, or the same index row (including INCLUDED columns) can also result in corruption of the index.
Foreign key, unique, and other user-defined constraints cannot be guaranteed for data that is written concurrently from both universes.

If the source universe fails, the target universe may end up in an inconsistent state where some transactions from the source have only some of their writes applied in the target (these are called _torn transactions_). This inconsistency does not automatically heal over time and may need to be manually resolved.


### Transactional replication

In this mode, reads occur at a time sufficiently in the past (typically 1-2 seconds) to ensure that all relevant data from the source universe has already been replicated. Additionally, writes to the target universe are not allowed.

Reads occur as of the _xCluster safe time_, ensuring that all writes from all source transactions that will commit at or before the _xCluster safe time_ have been replicated to the target universe. This means we read as of a time far enough in the past that there cannot be new incoming commits at or before that time. This guarantees consistent reads and ensures source universe transactions become visible atomically. Note that the _xCluster safe time_ is not blocked by any in-flight or long-running source-universe transactions.

_xCluster safe time_ advances as replication proceeds but lags behind real-time by the current replication lag. This means, for example, if we write at 2:00:00 PM in the source universe and read at 2:00:01 PM in the target universe and replication lag is, say, five seconds, then the read may read as of 1:59:56 PM and will not see the write. We may not be able to see the write until 2:00:06 PM in the target universe, assuming the replication lag remains at five seconds.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

If the source universe fails, we can discard all incomplete information in the target universe by rewinding it to the latest _xCluster safe time_ (1:59:56 PM in the example) using [Point-in-Time Recovery (PITR)](../../../manage/backup-restore/point-in-time-recovery/). The result will be a consistent database that includes only the transactions from the source universe that committed at or before the _xCluster safe time_. Unlike with non-transactional replication, there is no need to handle torn transactions.

Target universe read-only transactions run at serializable isolation level on a single consistent snapshot as of the _xCluster safe time_.

In xCluster transactional replication mode, writes to the target universe are not allowed. Consequently, this mode does not support bidirectional replication.

Transactional replication is currently only available for YSQL deployments.

Transactional replication comes in three modes:

#### Automatic mode

{{<tags/feature/ea idea="153">}}In this mode all aspects of replication are handled automatically, including schema changes.

#### Semi-automatic mode

Provides operationally simpler setup and management of replication compared to manual mode, as well as fewer steps for performing DDL changes. This is the recommended mode for new deployments.

{{<lead link="https://www.youtube.com/live/vYyn2OUSZFE?si=i3ZkBh6QqHKukB_p">}}
To learn more, watch [Simplified schema management with xCluster DB Scoped](https://www.youtube.com/live/vYyn2OUSZFE?si=i3ZkBh6QqHKukB_p)
{{</lead>}}

#### Manual mode

This mode is deprecated and not recommended for new deployments. It requires manual intervention for schema changes and is more complex to set up and manage.

## High-level implementation details

xCluster replicates WAL records from source universe tablets to target universe tablets. It is implemented by having _pollers_ in the target universe that poll the source universe tablet servers for WAL records. Each poller works independently and polls one source tablet, distributing the received changes among one or more target tablets. This allows xCluster to scale horizontally as more nodes are added.

The polled tablets examine only the WAL to determine recent changes rather than looking at their RocksDB instances. The incoming poll request specifies the WAL OpId to start gathering changes from, and the response includes a batch of changes and the WAL OpId to continue with next time.

The source universe periodically saves the OpId that the target universe has confirmed as processed. This information is stored in the `cdc_state` table.

{{<lead link="https://youtu.be/9TF3xPDDJ30?si=foKnj1CvDYidHqmx">}}
To learn more, watch [xCluster Replication](https://youtu.be/9TF3xPDDJ30?si=foKnj1CvDYidHqmx)
{{</lead>}}

### The mapping between source and target tablets

In simple cases, each target tablet can have a dedicated poller that directly polls the corresponding source tablet. However, in more complex scenarios, the number of tablets in the source and target universes may differ. Even if the number of tablets is the same, their sharding boundaries might not align due to historical tablet splits occurring at different points in time.

This means that each target tablet may require changes from multiple source tablets, and multiple target tablets may need changes from the same source tablet. To prevent redundant cross-universe reads from the same source tablet, only one poller reads from each source tablet. When a source tablet's changes are needed by multiple target tablets, the assigned poller distributes the changes to the relevant target tablets.

The following illustration shows an example of this setup for a single table:

![distribution of pollers to tablets](/images/architecture/replication/distribution-of-pollers-new.png)

In the illustration, the source universe is depicted on the left with three TServers (white boxes), each containing one tablet of the table (boxes inside) with specified ranges. The target universe is on the right, featuring one fewer TServer and tablet. The data from the top source tablet is distributed among both target tablets by the poller in the top target TServer. Meanwhile, the data from the remaining source tablets is replicated to the second target tablet by the pollers in the other target TServer. For simplicity, only the tablet leaders are shown, as pollers operate at and poll from leaders only.

Tablet splitting generates WAL records, which are replicated to the target side. This ensures that the mapping of pollers to source tablets is automatically updated as needed when a source tablet splits.

### Single-shard transactions

When a single-shard transaction commits, a single WAL record is generated that includes all the writes and the commit time for that transaction. This WAL record is then included in a batch of changes when the poller requests updates. Single-shard transactions only modify a single tablet.

Upon receiving the changes, the poller examines each write to determine the key it writes to and identifies the corresponding target tablet. The poller then forwards the writes to the appropriate tablets. The commit times of the writes are preserved, and the writes are marked as _external_. This marking prevents them from being further replicated by xCluster, whether onward to another cluster or back to the original cluster in bidirectional replication scenarios.

### Distributed transactions

Distributed transactions involve multiple WAL records and the transaction status tablet. Writes generate provisional records (intents) and corresponding WAL records linked to a transaction on the involved user tablets. The state of the transaction is tracked by one transaction status tablet. The transaction is committed by updating the transaction state in the transaction status tablet, which produces a WAL record. After the commit is made durable, all involved tablets are asynchronously informed to apply the transaction. This process converts provisional writes into regular writes and generates a further WAL record. The provisional records are made available for reads immediately after the commit, even if the apply has not occurred yet.

On the target universe, xCluster generates a special inert format for provisional records. This format omits the original row locking information and an additional index on the key in the intents DB, as these are unnecessary on the target side.

When a poller receives an apply WAL record, it distributes it to all the target tablets it manages. The transaction application on the target tablets mirrors that of the source universe. It converts the provisional writes into regular writes, maintaining the same commit time as on the source universe and marking them as external. At this stage, the transaction's writes to this tablet become visible for reads.

Because pollers operate independently and the writes to multiple tablets are not applied atomically, writes from a single transaction affecting multiple tablets can become visible at different times.

When a source transaction commits, it is applied to the relevant tablets lazily. This means that even though transaction _X_ commits before transaction _Y_, _X_'s apply WAL record may occur after _Y_'s apply WAL record on some tablets. If this happens, the writes from _X_ can become visible in the target universe after _Y_'s. This is why non-transactional mode reads are only eventually consistent and not timeline consistent.

### Transactional mode

Transactional mode addresses these issues by selecting an appropriate xCluster safe time.

The xCluster safe time for each database on the target universe is calculated as the minimum _xCluster apply safe time_ reached by any tablet in that database. Pollers use information from the source tablet leaders to determine their _xCluster apply safe time_. This time ensures that all transactions committed before it have been applied on the target tablets.

A source tablet leader determines the _xCluster apply safe time_ that the target poller can advance to based on the state of the apply operations of committed transactions. It periodically (every 250 ms) checks the state of in-flight transactions and generates apply WAL records for committed transactions. This ensures that the _xCluster apply safe time_ can keep advancing even when there are long-running transactions in the system.

{{<lead link="https://youtu.be/lI6gw7ncBs8?si=gAioZ_NgOyl2dsM5">}}
To learn more, watch [Transactional xCluster](https://youtu.be/lI6gw7ncBs8?si=gAioZ_NgOyl2dsM5)
{{</lead>}}

## Schema differences

{{< tip >}}
This section does not apply to Automatic mode, as Automatic mode automatically replicates schema changes.
{{< /tip >}}

xCluster replication requires that the source and target tables have identical schemas. This means that you cannot replicate data between tables if there are differences in their schemas, such as missing columns or columns with different data types. Ensuring schema consistency is crucial for the replication process to function correctly.

Additionally, this restriction includes hidden schema metadata, such as the assignment of column IDs. Even if two tables appear to have the same schema in YSQL, their schemas might not be identical. Therefore, in practice, the target table schema should be copied from the source table schema. For more details, refer to [replication bootstrapping](#replication-bootstrapping).

Because of this restriction, xCluster does not need to perform deep translations of row contents (such as dropping columns or translating column IDs within keys and values) when replicating rows between universes. This avoidance of deep translation reduces the replication cost and improves throughput.

Schema changes must be manually applied first to the source universe and then to the target universe. During this process, replication for the affected table is automatically paused when schema differences are detected and resumes once the schemas are identical.

## Replication bootstrapping

xCluster replicates the source WAL records to the target universe. WAL is garbage collected over time to conserve disk space. When setting up a new replication flow, the source universe may have already deleted some of the WAL records needed for an empty target universe to catch up. This is especially likely if the source universe has been running for a while and has accumulated a lot of WAL.

In this case, you need to bootstrap the target universe.

This process involves checkpointing the source universe to ensure that any new WAL records are preserved for xCluster. Following this, a [distributed backup](../../../manage/backup-restore/snapshot-ysql/#move-a-snapshot-to-external-storage) is performed and restored to the target universe. This not only copies all the data but also ensures that the table schemas are identical on both sides.

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

{{< Warning title="Important" >}}

Active-active multi-master replication can lead to inconsistencies if schemas or data modifications are not carefully coordinated across clusters.

Refer to [Inconsistencies affecting transactions](#inconsistencies-affecting-transactions) for details on how non-transactional mode can lead to inconsistencies.

This mode is not recommended for YSQL deployments.
{{< /Warning >}}

The following diagram illustrates this deployment:

![example of active-active deployment](/images/architecture/replication/active-active-deployment-new.png)

### Unsupported deployment scenarios

The following deployment scenarios are not yet supported:

- _Broadcast_: This topology involves one source universe sending data to many target universes, for example: `A -> B, A -> C`. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

- _Consolidation_: This topology involves many source universes sending data to one central target universe, for example: `B -> A, C -> A`. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

- _Daisy chaining_: This involves connecting a series of universes, for example: `A -> B -> C`

- _Star_: This involves connecting all universes to each other, for example: `A <-> B <-> C <-> A`

## Limitations

### YSQL

- `CREATE TABLE AS` and `SELECT INTO` DDL statements are not supported. You can work around this by breaking the DDL into a `CREATE TABLE` followed by `INSERT SELECT`.

- Materialized views

  [Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) are not replicated by xCluster. When setting up replication for a database, materialized views need to be excluded. You can create them on the target universe after the replication is set up. When refreshing, make sure to refresh on both sides.

- Modifications of Array Types

  While xCluster is active, array types whose base types are row types, domains, and multi-ranges should not be created, altered, or dropped. Create these types before xCluster is set up. If you need to modify these types, you must first drop xCluster replication, make the necessary changes, and then re-enable xCluster via bootstrap. [#24079](https://github.com/yugabyte/yugabyte-db/issues/24079)

- Row types

  Table columns whose types involve row types are not supported.

- [pgvector](../../../additional-features/pg-extensions/extension-pgvector/) indexes are not supported.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
<li>
    <a href="#ysql-transactional" class="nav-link active" id="ysql-transactional-tab" data-bs-toggle="tab"
    role="tab" aria-controls="ysql-transactional" aria-selected="true">
    Transactional
    </a>
</li>
<li>
    <a href="#ysql-non-transactional" class="nav-link" id="ysql-non-transactional-tab" data-bs-toggle="tab"
    role="tab" aria-controls="ysql-non-transactional" aria-selected="false">
    Non-Transactional
    </a>
</li>
</ul>
<div class="tab-content">
<div id="ysql-transactional" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysql-transactional-tab">

- By default, no writes are allowed in the target universe.

  {{<tags/feature/ea idea="2136">}}You can allow writes to the target on an exception basis, overriding the default read-only behavior by setting the following YSQL configuration parameter before executing a DML operation:

  ```sql
  SET yb_non_ddl_txn_for_sys_tables_allowed = true
  ```

  This is intended strictly for specialized use cases, such as enabling tools like Flyway to update maintenance tables (for example, schema version trackers) on the replica.

  {{< warning title="Important" >}}
Improper use can compromise replication consistency and lead to data divergence. Use this setting only when absolutely necessary and with a clear understanding of its implications.
  {{< /warning >}}

- Backups

  Take backups on the source universe. Backups against the target universe are not transactionally consistent.

- CDC

  For moving data out of YugabyteDB, set up CDC on the xCluster source universe. CDC on the xCluster target universe is not supported.

#### Transactional Automatic mode

- Global objects like Users, Roles, and Tablespaces are not replicated. These objects must be manually managed on the standby universe.
- DDLs related to Materialized Views (`CREATE`, `DROP`, and `REFRESH`) are not replicated. You can manually run these on both universes by setting the YSQL configuration parameter `yb_xcluster_ddl_replication.enable_manual_ddl_replication` to `true`.
- `ALTER COLUMN TYPE`, `ADD COLUMN ... SERIAL`, and `ALTER LARGE OBJECT` DDLs are not supported.
- DDLs related to `PUBLICATION` and `SUBSCRIPTION` are not supported.
- Replication of colocated tables is not yet supported.  See {{<issue 25926>}}.
- Rewinding of sequences (for example, restarting a sequence so it will repeat values) is discouraged because it may not be fully rolled back during unplanned failovers.
- The `TRUNCATE` command is only supported in v2025.1.1 and later.
- While Automatic mode is active, you can only `CREATE`, `DROP`, or `ALTER` the following extensions: file_fdw, fuzzystrmatch, pgcrypto, postgres_fdw, sslinfo, uuid-ossp, hypopg, pg_stat_monitor, and pgaudit. All other extensions must be created _before_ setting up automatic mode.
- If using pg_partman on v2025.1.0 or earlier, enable the cron job on the source cluster only. After switchover or failover, move the cron job to the new primary. Refer to pg_partman [Limitations](../../../additional-features/pg-extensions/extension-pgpartman/#xcluster).

#### Transactional Semi-Automatic and Manual mode

- Schema changes are not automatically replicated. All DDL changes must be manually applied to both source and target universes. For more information, refer to [DDLs in semi-automatic mode](../../../deploy/multi-dc/async-replication/async-transactional-setup-semi-automatic/#making-ddl-changes) and [DDLs in manual mode](../../../deploy/multi-dc/async-replication/async-transactional-tables).

  An exception are DDLs related to PUBLICATION and SUBSCRIPTION, which should only be used on the source universe.

- `ALTER TABLE` DDLs that involve table rewrites (see [Alter table operations that involve a table rewrite](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite)) may not be performed while replication is running; you will need to drop replication, perform those DDL(s) on the source universe, then create replication again.

- The `TRUNCATE` command is not supported.

- Sequence data is not replicated by these modes. Serial columns use sequences internally. Avoid serial columns in primary keys, as both universes would generate the same sequence numbers, resulting in conflicting rows. It is recommended to use UUIDs instead.

- While xCluster is active, user-defined composite, enum, and range types and arrays of those types should not be created, altered, or dropped. Create these types before xCluster is set up. If you need to modify these types, you must first drop xCluster replication, make the necessary changes, and then re-enable xCluster via [bootstrap](#replication-bootstrapping).

- pg_partman requires additional setup in Semi-Automatic mode. Refer to pg_partman [Limitations](../../../additional-features/pg-extensions/extension-pgpartman/#xcluster).

  pg_partman is not supported in Manual mode.

</div>

<div id="ysql-non-transactional" class="tab-pane fade" role="tabpanel" aria-labelledby="ysql-non-transactional-tab">

- Consistency issues

  Refer to [Inconsistencies affecting transactions](#inconsistencies-affecting-transactions) for details on how non-transactional mode can lead to inconsistencies.

- Table rewrites

  `ALTER TABLE` DDLs that involve table rewrites (see [Alter table operations that involve a table rewrite](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite)) may not be performed while replication is running; you will need to drop replication, perform those DDL(s) on the source universe, then create replication again.

- Composite, enum, and range (array) types

  While xCluster is active, user-defined composite, enum, and range types and arrays of those types should not be created, altered, or dropped. Create these types before xCluster is set up. If you need to modify these types, you must first drop xCluster replication, make the necessary changes, and then re-enable xCluster via [bootstrap](#replication-bootstrapping).

- The `TRUNCATE` command is not supported.

- pg_partman

    pg_partman is supported but not recommended. Refer to pg_partman [Limitations](../../../additional-features/pg-extensions/extension-pgpartman/#xcluster).

#### Uni-directional

- Backups

  Take backups on the source universe. Backups against the target universe are not transactionally consistent.

- CDC

  For moving data out of YugabyteDB, set up CDC on the xCluster source universe. CDC on the xCluster target universe is not supported.

#### Bi-directional

- Triggers

  Because xCluster replication operates at the DocDB layer, it bypasses the query layer. So, only the database triggers on the source universe are fired, and the ones on the target side are not fired. It is recommended to avoid using the same triggers on both universes to avoid any confusion.

- Indexes and Constraints

  In active-active multi-master setups, unique constraints cannot be guaranteed. When conflicting writes to the same key occur from separate universes simultaneously, they can violate unique constraints or result in inconsistent indexes. For example, two conflicting writes might result in both rows being present in the main table, but only one row in the index.

  Note that if you attempt to insert the same row on both universes at the same time to a table that does not have a primary key, you will end up with two rows with the same data. This is the expected PostgreSQL behavior — tables without primary keys can have multiple rows with the same data.

- Sequences and Serial columns

  Sequence data is not replicated by non-transactional xCluster. Serial columns use sequences internally. Avoid serial columns in primary keys, as both universes would generate the same sequence numbers, resulting in conflicting rows. It is recommended to use UUIDs instead.

- Backups

  Stop your workload on one side, wait for draining to complete, and take a backup on the still-running side.

- CDC

  CDC is not supported.

</div>

</div>

### YCQL

#### Transactional

YCQL is not currently supported.

#### Non-Transactional

##### Uni-directional

- Backups

  Take backups on the source universe. Backups against the target universe are not transactionally consistent.

##### Bi-directional

- Backups

  Stop your workload on one side, wait for draining to complete, and take a backup on the still-running side.

### Kubernetes

xCluster replication can be set up with Kubernetes-deployed universes. However, the source and target must be able to communicate by directly referencing the pods in the other universe. In practice, this either means that the two universes must be part of the same Kubernetes cluster or that two Kubernetes clusters must have DNS and routing properly set up amongst themselves.

Having two YugabyteDB universes, each in their own standalone Kubernetes cluster, communicating with each other via a load balancer, is not currently supported. See [#2422](https://github.com/yugabyte/yugabyte-db/issues/2422) for details.
