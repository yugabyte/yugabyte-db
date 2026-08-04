---
title: Enhanced PostgreSQL Compatibility Mode
linkTitle: Enhanced PG compatibility
description: Enhance your application performance for PostgreSQL parity
menu:
  v2.25:
    identifier: ysql-postgresql-compatibility
    parent: configuration
    weight: 3500
type: docs
rightNav:
  hideH3: true
---

YugabyteDB is a [PostgreSQL-compatible](https://www.yugabyte.com/tech/postgres-compatibility/) distributed database that supports the majority of PostgreSQL syntax. YugabyteDB is methodically expanding its features to deliver PostgreSQL-compatible performance that can substantially improve your application's efficiency.

To test and take advantage of features developed for enhanced PostgreSQL compatibility in YugabyteDB that are currently in {{<tags/feature/ea>}}, you can enable Enhanced PostgreSQL Compatibility Mode (EPCM). When this mode is turned on, YugabyteDB is configured to use all the latest features developed for feature and performance parity. EPCM is available in {{<release "2024.1">}} and later. The following features are part of EPCM.

| Feature | Flag/Configuration Parameter | EA | GA |
| :--- | :--- | :--- | :--- |
| [Read committed](#read-committed) | [yb_enable_read_committed_isolation](../yb-tserver/#ysql-default-transaction-isolation) | {{<release "2.20, 2024.1">}} | {{<release "2024.2.2">}} |
| [Wait-on-conflict](#wait-on-conflict-concurrency) | [enable_wait_queues](../yb-tserver/#enable-wait-queues) | {{<release "2.20">}} | {{<release "2024.1">}} |
| [Cost-based optimizer](#cost-based-optimizer) | [yb_enable_cbo](../yb-tserver/#yb-enable-cbo) | {{<release "2024.1">}} | {{<release "2025.1">}} |
| [Batch nested loop join](#batched-nested-loop-join) | [yb_enable_batchednl](../yb-tserver/#yb-enable-batchednl) | {{<release "2.20">}} | {{<release "2024.1">}} |
| [Ascending indexing by default](#default-ascending-indexing) | [yb_use_hash_splitting_by_default](../yb-tserver/#yb-use-hash-splitting-by-default) | {{<release "2024.1">}} | |
| [YugabyteDB bitmap scan](#yugabytedb-bitmap-scan) | [yb_enable_bitmapscan](../yb-tserver/#yb-enable-bitmapscan) | {{<release "2024.1.3">}} | {{<release "2025.1">}} |
| [Efficient communication<br>between PostgreSQL and DocDB](#efficient-communication-between-postgresql-and-docdb) | [pg_client_use_shared_memory](../yb-tserver/#pg-client-use-shared-memory) | {{<release "2024.1">}} | {{<release "2024.2">}} |
| [Parallel query](#parallel-query)<br>- Parallel append<br>- Parallel query | <br>[yb_enable_parallel_append](../../../additional-features/parallel-query/)<br>[yb_parallel_range_rows](../../../additional-features/parallel-query/) | {{<release "2024.2.3">}} | {{<release "2025.1">}} |

Note that Wait-on-conflict concurrency and Batched nested loop join are enabled by default in v2024.1 and later.

## Feature availability

After turning this mode on, as you upgrade universes, YugabyteDB will automatically enable new designated PostgreSQL compatibility features.

As features included in the PostgreSQL compatibility mode transition from {{<tags/feature/ea>}} to {{<tags/feature/ga>}} in subsequent versions of YugabyteDB, they are no longer managed under EPCM on your existing universes after the upgrade.

{{<note title="Note">}}
If you have set these features independent of EPCM, you cannot use EPCM.

Conversely, if you are using EPCM on a universe, you cannot set any of the features independently.
{{</note>}}

## Released features

The following features are currently available in EPCM.

### Read committed

Flag: `yb_enable_read_committed_isolation=true`

Read Committed isolation level handles serialization errors and avoids the need to retry errors in the application logic. Read Committed provides feature compatibility, and is the default isolation level in PostgreSQL. When migrating applications from PostgreSQL to YugabyteDB, read committed is the preferred isolation level.

{{<lead link="../../../architecture/transactions/read-committed/">}}
To learn about read committed isolation, see [Read Committed](../../../architecture/transactions/read-committed/).
{{</lead>}}

### Cost-based optimizer

Configuration parameter: `yb_enable_cbo=on`

[Cost-based optimizer (CBO)](../../../architecture/query-layer/planner-optimizer/) creates optimal execution plans for queries, providing significant performance improvements both in single-primary and distributed PostgreSQL workloads. This feature reduces or eliminates the need to use hints or modify queries to optimize query execution. CBO provides improved performance parity.

For information on configuring CBO, refer to [Enable cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/).

When enabling this parameter, you must run ANALYZE on user tables to maintain up-to-date statistics.

{{<lead link="../../../architecture/query-layer/planner-optimizer/">}}
To learn how CBO works, see [Query Planner / CBO](../../../architecture/query-layer/planner-optimizer/)
{{</lead>}}

#### Wait-on-conflict concurrency

Flag: `enable_wait_queues=true`

Enables use of wait queues so that conflicting transactions can wait for the completion of other dependent transactions, helping to improve P99 latencies. Wait-on-conflict concurrency control provides feature compatibility, and uses the same semantics as PostgreSQL.

{{<lead link="../../../architecture/transactions/concurrency-control/">}}
To learn about concurrency control in YugabyteDB, see [Concurrency control](../../../architecture/transactions/concurrency-control/).
{{</lead>}}

### Batched nested loop join

Configuration parameter: `yb_enable_batchednl=true`

Batched nested loop join (BNLJ) is a join execution strategy that improves on nested loop joins by batching the tuples from the outer table into a single request to the inner table. By using batched execution, BNLJ helps reduce the latency for query plans that previously used nested loop joins. BNLJ provides improved performance parity.

{{<lead link="../../../architecture/query-layer/join-strategies/">}}
To learn about join strategies in YugabyteDB, see [Join strategies](../../../architecture/query-layer/join-strategies/).
{{</lead>}}

### Default ascending indexing

Configuration parameter: `yb_use_hash_splitting_by_default=false`

Enable efficient execution for range queries on data that can be sorted into some ordering. In particular, the query planner will consider using an index whenever an indexed column is involved in a comparison using one of the following operators: `<   <=   =   >=   >`.

Also enables retrieving data in sorted order, which can eliminate the need to sort the data.

Default ascending indexing provides feature compatibility and is the default in PostgreSQL.

### YugabyteDB bitmap scan

Configuration parameter: `yb_enable_bitmapscan=true`

Bitmap scans use multiple indexes to answer a query, with only one scan of the main table. Each index produces a "bitmap" indicating which rows of the main table are interesting. Bitmap scans can improve the performance of queries containing `AND` and `OR` conditions across several index scans. YugabyteDB bitmap scan provides feature compatibility and improved performance parity. For YugabyteDB relations to use a bitmap scan, the PostgreSQL parameter `enable_bitmapscan` must also be true (the default).

### Efficient communication between PostgreSQL and DocDB

Configuration parameter: `pg_client_use_shared_memory=true`

Enable more efficient communication between YB-TServer and PostgreSQL using shared memory. This feature provides improved performance parity.

### Parallel query

{{< note title="Note" >}}

Parallel query has not been added to EPCM.

{{< /note >}}

Configuration parameters:

- Parallel query - `yb_parallel_range_rows`
- Parallel append - `yb_enable_parallel_append=true`

Enables the use of [PostgreSQL parallel queries](https://www.postgresql.org/docs/15/parallel-query.html). Using parallel queries, the query planner can devise plans that leverage multiple CPUs to answer queries faster. Currently, YugabyteDB supports parallel query for colocated tables. Support for hash- and range-sharded tables is planned. Parallel query provides feature compatibility and improved performance parity.

{{<lead link="../../../additional-features/parallel-query/">}}
To learn about using parallel queries, see [Parallel queries](../../../additional-features/parallel-query/).
{{</lead>}}

## Enable EPCM

### YugabyteDB

To enable EPCM in YugabyteDB:

- Pass the `enable_pg_parity_early_access` flag to [yugabyted](../yugabyted/) when starting your cluster.

For example, from your YugabyteDB home directory, run the following command:

```sh
./bin/yugabyted start --enable_pg_parity_early_access
```

### YugabyteDB Anywhere

To enable EPCM in YugabyteDB Anywhere v2024.1, see the [Release notes](/preview/releases/yba-releases/v2024.1/#v2024.1.0.0).

To enable EPCM in YugabyteDB Anywhere v2024.2 or later:

- When creating a universe, turn on the **Enable Enhanced Postgres Compatibility** option.

  You can also change the setting on deployed universes using the **More > Edit Postgres Compatibility** option.

{{<warning title="Flag settings">}}
Setting Enhanced Postgres Compatibility overrides any [flags you set](../../../yugabyte-platform/manage-deployments/edit-config-flags/) individually for the universe. The **G-Flags** tab will however continue to display the setting that you customized.
{{</warning>}}

### YugabyteDB Aeon

To enable EPCM in YugabyteDB Aeon:

1. When creating a cluster, choose a track with database v2024.1.0 or later.
1. Select the **Enhanced Postgres Compatibility** option (on by default).

You can also change the setting on the **Settings** tab for deployed clusters.

## Unsupported PostgreSQL features

Although YugabyteDB aims to be fully compatible with PostgreSQL, supporting all PostgreSQL features in a distributed system is not always feasible. This section documents the known list of differences between PostgreSQL and YugabyteDB. You need to consider these differences while porting an existing application to YugabyteDB.

The following PostgreSQL features are not supported in YugabyteDB:

| Unsupported PostgreSQL feature      | Track feature request GitHub issue |
| ----------- | ----------- |
| LOCK TABLE to obtain a table-level lock | {{<issue 5384>}}|
| Table inheritance    | {{<issue 5956>}}|
| Exclusion constraints | {{<issue 3944>}}|
| Deferrable constraints | {{<issue 1709>}}|
| Constraint Triggers|{{<issue 4700>}}|
| GiST indexes | {{<issue 1337>}}|
| Events (Listen/Notify) | {{<issue 1872>}}|
| XML Functions | {{<issue 1043>}}|
| XA syntax | {{<issue 11084>}}|
| ALTER TYPE | {{<issue 1893>}}|
| CREATE CONVERSION | {{<issue 10866>}}|
| Primary/Foreign key constraints on foreign tables | {{<issue 10698>}}, {{<issue 10699>}} |
| GENERATED ALWAYS AS STORED columns | {{<issue 10695>}}|
| Multi-column GIN indexes| {{<issue 10652>}}|
| CREATE ACCESS METHOD | {{<issue 10693>}}|
| DESC/HASH on GIN indexes (ASC supported) | {{<issue 10653>}}|
| CREATE SCHEMA with elements | {{<issue 10865>}}|
| Index on citext column | {{<issue 9698>}}|
| ABSTIME type | {{<issue 15637>}}|
| transaction ids (xid) <br/> YugabyteDB uses [Hybrid logical clocks](../../../architecture/transactions/transactions-overview/#hybrid-logical-clocks) instead of transaction ids. | {{<issue 15638>}}|
| DDL operations within transaction| {{<issue 1404>}}|
| Some ALTER TABLE variants| {{<issue 1124>}}|
| UNLOGGED table | {{<issue 1129>}} |
| Indexes on complex datatypes such as INET, CITEXT, JSONB, ARRAYs, and so on.| {{<issue 9698>}}, {{<issue 23829>}}, {{<issue 17017>}} |
| %TYPE syntax in Functions/Procedures/Triggers|{{<issue 23619>}}|
| Storage parameters on indexes or constraints|{{<issue 23467>}}|
| REFERENCING clause for triggers | {{<issue 1668>}}|
| BEFORE ROW triggers on partitioned tables | {{<issue 24830>}}|
