---
title: View CREATE INDEX status with pg_stat_progress_create_index
linkTitle: View CREATE INDEX status
description: Use pg_stat_progress_create_index to get the CREATE INDEX command status, including the status of an ongoing concurrent index backfill, and the index build's progress reports.
headerTitle: View CREATE INDEX status with pg_stat_progress_create_index
image: /images/section_icons/index/develop.png
menu:
  v2.14:
    identifier: pg-stat-progress-create-index
    parent: query-tuning
    weight: 450
type: docs
---

You can add a new index to an existing table using the YSQL [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index/#semantics) statement. YugabyteDB supports [online index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md), which is enabled by default. Using this feature, you can build indexes on non-empty tables online, without failing other concurrent writes. YugabyteDB also supports the [CREATE INDEX NONCONCURRENTLY](../../../api/ysql/the-sql-language/statements/ddl_create_index/#nonconcurrently) statement to disable online index backfill.

### pg_stat_progress_create_index

YugabyteDB supports the PostgreSQL `pg_stat_progress_create_index` view to report the progress of the CREATE INDEX command execution. The view contains one row for each backend connection that is currently running a CREATE INDEX command, and the row entry is cleared after the completion of command execution.

The `pg_stat_progress_create_index` view can provide the following details:

- Number of rows processed during an index backfill.
- The current phase of the command with `initializing` or `backfilling` as the possible phases.
- Index progress report for all the different configurations of an index or index build such as non-concurrent index builds, GIN indexes, partial indexes, and include indexes.

The following table describes the view columns:

| Column | Type | Description |
| :----- | :--- | :---------- |
| pid | integer | Process ID of backend that is running the `CREATE INDEX`. |
| datid | OID | Object ID of the database to which this backend is connected. |
| datname | name | Name of the database to which this backend is connected. |
| relid | OID | Object ID of the indexed relation.|
| index_relid | OID | Object ID of the index. |
| command | text | The command that is running `CREATE INDEX CONCURRENTLY`, or `CREATE INDEX NONCONCURRENTLY`. |
| phase | text | The current phase of the command. The possible phases are _initializing_, or _backfilling_. |
| tuples_total | bigint | Number of indexed table tuples already processed. |
| tuples_done | bigint | Estimate of total number of tuples (in the indexed table). This value is retrieved from `pg_class.reltuples`. |
| partitions_total | bigint | If the ongoing `CREATE INDEX` is for a partitioned table, this refers to the total number of partitions in the table. Set to 0 otherwise. |
| partitions_done | bigint | If the ongoing `CREATE INDEX` is for a partitioned table, this refers to the number of partitions the index has been created for. Set to 0 otherwise. |

Columns such as `lockers_total`, `lockers_done`, `current_locker_pid`, `blocks_total`, and `blocks_done` are not applicable to YugabyteDB and always have null values.

## YugabyteDB-specific changes

The `pg_stat_progress_create_index` view includes the following YugabyteDB-specific changes:

- In YugabyteDB, the `pg_stat_progress_create_index` view is a local view; it only has entries for CREATE INDEX commands issued by local YSQL clients.

- In PostgreSQL, `tuples_done` and `tuples_total` refer to the tuples of the _index_. However, in YugabyteDB, these fields refer to the tuples of the _indexed table_. This discrepancy is only observed for partial indexes, where the reported progress is less than the actual progress. `tuples_total` is an estimate that is retrieved from `pg_class.reltuples`.

- In YugabyteDB, `tuples_done` and `tuples_total` are not displayed (set to null) for temporary indexes.

## Example

The following example demonstrates the possible phases (initializing, backfilling) for the CREATE INDEX operation using the `pg_stat_progress_create_index` view.

{{% explore-setup-single %}}

1. From your local YugabyteDB installation directory, connect to the [YSQL](../../../admin/ysqlsh/) shell, and create an index on an existing table as follows:

    ```sql
    CREATE TABLE customers(id int, customer_name text);
    CREATE INDEX ON customers(customer_name);
    ```

1. On a separate parallel YSQL connection on the same node, select from the view to see the progress of the command as follows:

    ```sql
    SELECT * FROM pg_stat_progress_create_index;
    ```

    ```output
    pid   | datid | datname  | relid | index_relid |       command             |    phase     | lockers_total | lockers_done | current_locker_pid | blocks_total | blocks_done | tuples_total | tuples_done | partitions_total | partitions_done
    ------+-------+----------+-------+-------------+---------------------------+--------------+---------------+--------------+--------------------+--------------+-------------+--------------+-------------+------------------+-----------------
    78841 | 13291 | yugabyte | 16384 |       16390 | CREATE INDEX CONCURRENTLY | initializing |               |              |                    |              |             |       100000 |           0 |                0 |               0
    (1 row)
    ```

    ```sql
    SELECT * FROM pg_stat_progress_create_index;
    ```

    ```output
    pid   | datid | datname  | relid | index_relid |          command          |    phase    | lockers_total | lockers_done | current_locker_pid | blocks_total | blocks_done | tuples_total | tuples_done | partitions_total | partitions_done
    ------+-------+----------+-------+-------------+---------------------------+-------------+---------------+--------------+--------------------+--------------+-------------+--------------+-------------+------------------+-----------------
    77444 | 13291 | yugabyte | 16404 |       16412 | CREATE INDEX CONCURRENTLY | backfilling |               |              |                    |              |             |       100000 |       49904 |                0 |               0
    (1 row)
    ```

## Learn more

- Refer to [View live queries with pg_stat_activity](../pg-stat-activity/) to analyze live queries.
- Refer to [View COPY progress with pg_stat_progress_copy](../pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
- Refer to [Optimize YSQL queries using pg_hint_plan](../pg-hint-plan/) show the query execution plan generated by YSQL.
- Refer to [Get query statistics using pg_stat_statements](../pg-stat-statements/) to track planning and execution of all the SQL statements.
