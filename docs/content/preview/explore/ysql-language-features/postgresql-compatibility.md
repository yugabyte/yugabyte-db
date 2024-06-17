---
title: PostgreSQL compatibility
linkTitle: PostgreSQL compatibility
description: Summary of YugabyteDB's PostgreSQL compatibility
aliases:
  - /preview/ysql/postgresql-compatibility/
menu:
  preview:
    identifier: explore-ysql-postgresql-compatibility
    parent: explore-ysql-language-features
    weight: 70
type: docs
---

YugabyteDB is a [PostgreSQL-compatible](https://www.yugabyte.com/tech/postgres-compatibility/) distributed database that supports the majority of PostgreSQL syntax. This means that existing applications built on PostgreSQL can often be migrated to YugabyteDB without changing application code.

Because YugabyteDB is PostgreSQL compatible, it works with the majority of PostgreSQL database tools such as various language drivers, ORM tools, schema migration tools, and many more third-party database tools.

## PostgreSQL parity

PostgreSQL parity is shorthand for two concepts:

- Feature compatibility

    Compatibility refers to whether YugabyteDB supports all the features of PostgreSQL and behaves as PostgreSQL does. With full PostgreSQL compatibility, you should be able to take an application running on PostgreSQL and run it on YugabyteDB without any code changes. The application will run without any errors, but it may not perform well because of the distributed nature of YugabyteDB.

- Performance parity

    Performance parity refers to the capabilities of YugabyteDB that allow applications running on PostgreSQL to run with predictable performance on YugabyteDB. In other words, the performance degradation experienced by small and medium scale applications going from a single server database to a distributed database should be predictable and bounded. For example, our goal is to have not more than 3x latency penalty due to distribution.

### Enhanced Postgres Compatibility Mode

{{<badge/ea>}} This mode enables you to take advantage of many early access improvements in PostgreSQL parity in YugabyteDB, making it even easier to lift and shift your applications from PostgreSQL. When this mode is turned on, YugabyteDB is configured to use the latest features developed for feature and performance parity.

Enhanced Postgres Compatibility Mode is managed using the `enable_pg_parity_early_access` flag.

In YugabyteDB Anywhere, you manage this setting using the **Enhanced Postgres Compatibility Mode** option when creating a universe.

In YugabyteDB Aeon, you manage this setting using the **Enhanced Postgres Compatibility Mode** option when creating a cluster.

Depending on the version of YugabyteDB, this flag enables different PG parity features as described in the following table.

| YugabyteDB Version | Feature | Flag |
| :--- | :--- | :--- |
| 2024.1 | Read-Committed isolation mode | yb_enable_read_committed_isolation |
|        | Wait-on-Conflict concurrency mode for predictable P99 latencies |
|        | Cost Based Optimizer takes advantage of the distributed storage layer architecture and includes query pushdowns, LSM indexes, and batched nested loop joins to offer PostgreSQL-like performance. |

yb_enable_read_committed_isolation=true
ysql_enable_read_request_caching=true
"ysql_pg_conf_csv": "yb_enable_base_scans_cost_model=true,"
                      "yb_bnl_batch_size=1024,"
                      "yb_fetch_row_limit=0,"
                      "yb_fetch_size_limit=1MB,"
                      "yb_use_hash_splitting_by_default=false"

Note: When enabling the cost models, ensure that packed row for colocated tables is enabled by setting the --ysql_enable_packed_row_for_colocated_table flag to true.

To enable compatibility mode:

- Passing the `enable_pg_parity_early_access` flag to yugabyted when bringing up your cluster.

For example, from your YugabyteDB home directory, run the following command:

```sh
./bin/yugabyted start --enable_pg_parity_early_access
```

Note: When enabling the cost models, ensure that packed row for colocated tables is enabled by setting the --ysql_enable_packed_row_for_colocated_table flag to true.

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
