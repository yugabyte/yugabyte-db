---
title: PostgreSQL compatibility
linkTitle: PostgreSQL compatibility
description: Summary of YugabyteDB's PostgreSQL compatibility
aliases:
  - /preview/ysql/postgresql-compatibility/
  - /preview/explore/ysql-language-features/postgresql-compatibility/
menu:
  preview:
    identifier: explore-ysql-postgresql-compatibility
    parent: explore-ysql-language-features
    weight: 70
type: docs
rightNav:
  hideH4: true
---

YugabyteDB is a [PostgreSQL-compatible](https://www.yugabyte.com/tech/postgres-compatibility/) distributed database that supports the majority of PostgreSQL syntax. This means that existing applications built on PostgreSQL can often be migrated to YugabyteDB without changing application code.

Because YugabyteDB is PostgreSQL compatible, it works with the majority of PostgreSQL database tools such as various language drivers, ORM tools, schema migration tools, and many more third-party database tools.

PostgreSQL compatibility has two aspects:

- Feature compatibility

    Compatibility refers to whether YugabyteDB supports all the features of PostgreSQL and behaves as PostgreSQL does. With full PostgreSQL compatibility, you should be able to take an application running on PostgreSQL and run it on YugabyteDB without any code changes. The application will run without any errors, but it may not perform well because of the distributed nature of YugabyteDB.

- Performance parity

    Performance parity refers to the capabilities of YugabyteDB that allow applications running on PostgreSQL to run with predictable performance on YugabyteDB. In other words, the performance degradation experienced by small and medium scale applications going from a single server database to a distributed database should be predictable and bounded.

## Enhanced PostgreSQL Compatibility Mode

To test and take advantage of features developed for PostgreSQL compatibility in YugabyteDB that are currently in {{<badge/ea>}}, you can enable Enhanced PostgreSQL Compatibility Mode (EPCM). When this mode is turned on, YugabyteDB is configured to use all the latest features developed for feature and performance parity. EPCM is available in [v2024.1](/preview/releases/ybdb-releases/v2024.1/) and later.

<!--Depending on the version of YugabyteDB, EPCM configures a different set of features as described in the following sections.-->

After turning this mode on, as you upgrade universes, YugabyteDB will automatically enable new designated PostgreSQL compatibility features.

As features included in the PostgreSQL compatibility mode transition from {{<badge/ea>}} to {{<badge/ga>}} in subsequent versions of YugabyteDB, they become enabled by default on new universes, and are no longer managed under EPCM on your existing universes after the upgrade.

{{<note title="Note">}}
If you have set these features independent of EPCM, you cannot use EPCM.

Conversely, if you are using EPCM on a universe, you cannot set any of the features independently.
{{</note>}}

| Feature | Flag/Configuration Parameter | EA | Included in EPCM |
| :--- | :--- | :--- | :--- |
| Read committed | [yb_enable_read_committed_isolation](../../../reference/configuration/yb-tserver/#ysql-default-transaction-isolation) | v2.20 and 2024.1 | Yes |
| Wait-on-conflict | [enable_wait_queues](../../../reference/configuration/yb-tserver/#enable-wait-queues) | v2024.1 | Yes |
| Cost-based optimizer | [yb_enable_base_scans_cost_model](../../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) | v2.20 | Yes |
| Batch nested loop join | [yb_enable_batchednl](../../../reference/configuration/yb-tserver/#yb-enable-batchednl) | v2.20 and 2024.1 | Yes |
| Efficient communication<br>between PostgreSQL and DocDB | [pg_client_use_shared_memory](../../../reference/configuration/yb-tserver/#pg-client-use-shared-memory) | 2024.1 | Yes |
| Ascending indexing by default | [yb_use_hash_splitting_by_default](../../../reference/configuration/yb-tserver/#yb-use-hash-splitting-by-default) | 2024.1 | Yes |
| Bitmap scan | [enable_bitmapscan](../../../reference/configuration/yb-tserver/#enable-bitmapscan) | 2024.1.3 | Planned |
| Parallel query | | | Planned |

### Released

The following features are currently available.

#### Read committed

Flag: `yb_enable_read_committed_isolation=true`

Read Committed isolation level handles serialization errors and avoids the need to retry errors in the application logic. Read Committed provides feature compatibility, and is the default isolation level in PostgreSQL. When migrating applications from PostgreSQL to YugabyteDB, read committed is the preferred isolation level.

{{<lead link="../../../architecture/transactions/read-committed/">}}
To learn more about read committed isolation, see [Read Committed](../../../architecture/transactions/read-committed/).
{{</lead>}}

#### Cost-based optimizer

Configuration parameter: `yb_enable_base_scans_cost_model=true`

Cost-based optimizer (CBO) creates optimal execution plans for queries, providing significant performance improvements both in single-primary and distributed PostgreSQL workloads. This feature reduces or eliminates the need to use hints or modify queries to optimize query execution. CBO provides improved performance parity.

Note: When enabling this parameter, you must run ANALYZE on user tables to maintain up-to-date statistics.

Note: When enabling the cost models, ensure that packed row for colocated tables is enabled by setting the `--ysql_enable_packed_row_for_colocated_table` flag to true.

#### Wait-on-conflict concurrency

Flag: `enable_wait_queues=true`

Enables use of wait queues so that conflicting transactions can wait for the completion of other dependent transactions, helping to improve P99 latencies. Wait-on-conflict concurrency control provides feature compatibility, and uses the same semantics as PostgreSQL.

{{<lead link="../../../architecture/transactions/concurrency-control/">}}
To learn more about concurrency control in YugabyteDB, see [Concurrency control](../../../architecture/transactions/concurrency-control/).
{{</lead>}}

#### Batched nested loop join

Configuration parameter: `yb_bnl_batch_size=1024`

Batched nested loop join (BNLJ) is a join execution strategy that improves on nested loop joins by batching the tuples from the outer table into a single request to the inner table. By using batched execution, BNLJ helps reduce the latency for query plans that previously used nested loop joins. BNLJ provides improved performance parity.

{{<lead link="../join-strategies/">}}
To learn more about join strategies in YugabyteDB, see [Join strategies](../../../architecture/transactions/concurrency-control/).
{{</lead>}}

#### Efficient communication between PostgreSQL and DocDB

Configuration parameter: `pg_client_use_shared_memory=true`

Enable more efficient communication between YB-TServer and PostgreSQL using shared memory. This feature provides improved performance parity.

#### Default ascending indexing

Configuration parameter: `yb_use_hash_splitting_by_default=false`

Enable efficient execution for range queries on data that can be sorted into some ordering. In particular, the query planner will consider using an index whenever an indexed column is involved in a comparison using one of the following operators: `<   <=   =   >=   >`.

Also enables retrieving data in sorted order, which can eliminate the need to sort the data.

Default ascending indexing provides feature compatibility and is the default in PostgreSQL.

### Planned

The following features are planned for EPCM in future releases.

#### Bitmap scan

Configuration parameter: `enable_bitmapscan=true`

Bitmap scans use multiple indexes to answer a query, with only one scan of the main table. Each index produces a "bitmap" indicating which rows of the main table are interesting. Bitmap scans can improve the performance of queries containing AND and OR conditions across several index scans. Bitmap scan provides improved performance parity.

#### Parallel query

Enables the use of PostgreSQL [parallel queries](https://www.postgresql.org/docs/11/parallel-query.html). Using parallel queries, the query planner can devise plans that leverage multiple CPUs to answer queries faster. Parallel query provides feature compatibility and improved performance parity.

### Enable EPCM

#### YugabyteDB

To enable EPCM in YugabyteDB:

- Pass the `enable_pg_parity_early_access` flag to [yugabyted](../../../reference/configuration/yugabyted/) when starting your cluster.

For example, from your YugabyteDB home directory, run the following command:

```sh
./bin/yugabyted start --enable_pg_parity_early_access
```

Note: When enabling the cost models, ensure that packed row for colocated tables is enabled by setting the `--ysql_enable_packed_row_for_colocated_table` flag to true.

#### YugabyteDB Anywhere

To enable EPCM in YugabyteDB Anywhere v2024.1, see the [Release notes](/preview/releases/yba-releases/v2024.1/#highlights).

To enable EPCM in YugabyteDB Anywhere v2024.2 or later:

- When creating a universe, turn on the **Enable Enhanced Postgres Compatibility** option.

  You can also change the setting on deployed universes using the **More > Edit Postgres Compatibility** option.

{{<warning title="Flag settings">}}
Setting Enhanced Postgres Compatibility overrides any [flags you set](../../../yugabyte-platform/manage-deployments/edit-config-flags/) individually for the universe. The **G-Flags** tab will however continue to display the setting that you customized.
{{</warning>}}

#### YugabyteDB Aeon

To enable EPCM in YugabyteDB Aeon:

1. When creating a cluster, choose a track with database v2024.1.0 or later.
1. Select the **Enhanced Postgres Compatibility** option (on by default).

You can also change the setting on the **Settings** tab for deployed clusters.

## Unsupported PostgreSQL features

Because YugabyteDB is a distributed database, supporting all PostgreSQL features in a distributed system is not always feasible. This section documents the known list of differences between PostgreSQL and YugabyteDB. You need to consider these differences while porting an existing application to YugabyteDB.

The following PostgreSQL features are not supported in YugabyteDB:

| Unsupported PostgreSQL feature      | Track feature request GitHub issue |
| ----------- | ----------- |
| LOCK TABLE to obtain a table-level lock | [5384](https://github.com/yugabyte/yugabyte-db/issues/5384)|
| Table inheritance    | [5956](https://github.com/yugabyte/yugabyte-db/issues/5956)|
| Exclusion constraints | [3944](https://github.com/yugabyte/yugabyte-db/issues/3944)|
| Deferrable constraints | [1709](https://github.com/yugabyte/yugabyte-db/issues/1709)|
| GiST indexes | [1337](https://github.com/yugabyte/yugabyte-db/issues/1337)|
| Events (Listen/Notify) | [1872](https://github.com/yugabyte/yugabyte-db/issues/1872)|
| XML Functions | [1043](https://github.com/yugabyte/yugabyte-db/issues/1043)|
| XA syntax | [11084](https://github.com/yugabyte/yugabyte-db/issues/11084)|
| ALTER TYPE | [1893](https://github.com/yugabyte/yugabyte-db/issues/1893)|
| CREATE CONVERSION | [10866](https://github.com/yugabyte/yugabyte-db/issues/10866)|
| Primary/Foreign key constraints on foreign tables | [10698](https://github.com/yugabyte/yugabyte-db/issues/10698), [10699](https://github.com/yugabyte/yugabyte-db/issues/10699) |
| GENERATED ALWAYS AS STORED columns | [10695](https://github.com/yugabyte/yugabyte-db/issues/10695)|
| Multi-column GIN indexes| [10652](https://github.com/yugabyte/yugabyte-db/issues/10652)|
| CREATE ACCESS METHOD | [10693](https://github.com/yugabyte/yugabyte-db/issues/10693)|
| DESC/HASH on GIN indexes (ASC supported) | [10653](https://github.com/yugabyte/yugabyte-db/issues/10653)|
| CREATE SCHEMA with elements | [10865](https://github.com/yugabyte/yugabyte-db/issues/10865)|
| Index on citext column | [9698](https://github.com/yugabyte/yugabyte-db/issues/9698)|
| ABSTIME type | [15637](https://github.com/yugabyte/yugabyte-db/issues/15637)|
| transaction ids (xid) <br/> YugabyteDB uses [Hybrid logical clocks](../../../architecture/transactions/transactions-overview/#hybrid-logical-clocks) instead of transaction ids. | [15638](https://github.com/yugabyte/yugabyte-db/issues/15638)|
| DDL operations within transaction| [1404](https://github.com/yugabyte/yugabyte-db/issues/1404)|
| Some ALTER TABLE variants| [1124](https://github.com/yugabyte/yugabyte-db/issues/1124)|
