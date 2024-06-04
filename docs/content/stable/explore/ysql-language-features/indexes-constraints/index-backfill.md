---
title: Online index backfilling
linkTitle: Index backfill
description: Understand how to create indexes without affecting ongoing queries
headerTitle: Create indexes and track the progress
headcontent: Understand how YugabyteDB creates indexes without affecting ongoing queries
menu:
  stable:
    identifier: index-backfill
    parent: explore-indexes-constraints-ysql
    weight: 300
type: docs
---

You can add a new index to an existing table using the YSQL [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#semantics) statement. YugabyteDB supports [online index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md), which is enabled by default. Using this feature, you can build indexes on tables that already have data, without affecting other concurrent writes. YugabyteDB also supports the [CREATE INDEX NONCONCURRENTLY](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#nonconcurrently) statement to disable online index backfill.

## Tracking index creation

The current state of an index backfill can be viewed by executing the `pg_stat_progress_create_index` view to report the progress of the CREATE INDEX command execution. The view contains one row for each backend connection that is currently running a CREATE INDEX command, and the row entry is cleared after the completion of the command execution.

The `pg_stat_progress_create_index` view can provide the following details:

- Number of rows processed during an index backfill.
- The current phase of the command is either `initializing` or `backfilling`.
- Index progress report for all the different configurations of an index or index build such as non-concurrent index builds, GIN indexes, partial indexes, and include indexes.

{{<note>}}
Columns such as lockers_total, lockers_done, current_locker_pid, blocks_total, and blocks_done in the `pg_stat_progress_create_index` view do not apply to YugabyteDB and always have null values.
{{</note>}}

## Example

The following example demonstrates the possible phases (initializing, backfilling) for the CREATE INDEX operation using the `pg_stat_progress_create_index` view.

{{% explore-setup-single %}}

1. From your local YugabyteDB installation directory, connect to the [YSQL](../../../../admin/ysqlsh/) shell, and create an index on an existing table as follows:

    ```sql
    CREATE TABLE test(id int);
    ```

1. Populate the table with some data.

    ```sql
    INSERT INTO test(id) SELECT n FROM generate_series(1,1000000) AS n;
    ```

1. On a separate parallel YSQL connection on the same node, select from the view to see the progress of the command in a repeated fashion with `\watch 1` as follows:

    ```sql
    SELECT tbl.relname as tblname, idx.relname as indexname, command, phase, tuples_total, tuples_done
    FROM pg_stat_progress_create_index
    INNER JOIN pg_class as tbl on tbl.oid = relid
    INNER JOIN pg_class as idx on idx.oid = index_relid
    where tbl.relname = 'test';
    \watch 1
    ```

    Initially, you will see an empty output like this.

    ```output
       tblname | indexname | command | phase | tuples_total | tuples_done
      ---------+-----------+---------+-------+--------------+-------------
    ```

1. Now create an index on the `id` column.

    ```sql
    CREATE INDEX idx_id ON test(id);
    ```

1. On the other terminal, you will see the progress on the index creation first with the first phase, `initializing` as:

    ```tablegen
     tblname | indexname |          command          |    phase     | tuples_total | tuples_done
    ---------+-----------+---------------------------+--------------+--------------+-------------
     test    | idx_id    | CREATE INDEX CONCURRENTLY | initializing |            0 |           0
    ```

    And then you will see the index backfilling happen as:

    ```tablegen
      tblname | indexname |          command          |    phase    | tuples_total | tuples_done
     ---------+-----------+---------------------------+-------------+--------------+-------------
      test    | idx_id    | CREATE INDEX CONCURRENTLY | backfilling |            0 |           0
    ```

    You will see the tuples_done count increasing as the backfilling progresses. When the backfilling is done, you will see the `tuples_done` count to be updated correctly.

    ```tablegen
      tblname | indexname |          command          |    phase    | tuples_total | tuples_done
     ---------+-----------+---------------------------+-------------+--------------+-------------
      test    | idx_id    | CREATE INDEX CONCURRENTLY | backfilling |            0 |     1000000
    ```

## Memory usage

Backfilling consumes some amount of memory. The memory consumption is directly proportional to the data size per-row, the number of write operations batched together, and the number of parallel backfills. You can view the approximate memory usage by executing the following SQL statement via `ysqlsh`.

```sql
SELECT
 indexname, tablet, now()-backend_start started, pg_size_pretty(allocated_mem_bytes) mem, pg_size_pretty(rss_mem_bytes) rss
 FROM (
  SELECT
   allocated_mem_bytes, rss_mem_bytes, backend_start, query_start, query
   ,regexp_replace(query,'(BACKFILL INDEX) ([0-9]*) .* PARTITION (.*);','\2')::oid  AS indexoid
   ,regexp_replace(query,'(BACKFILL INDEX) ([0-9]*) .* PARTITION (.*);','\3')::text AS tablet
  FROM pg_stat_activity
  WHERE query LIKE 'BACKFILL INDEX %'
 ) pg_stat_activity
 LEFT OUTER JOIN ( SELECT
  oid AS indexoid, relname AS indexname FROM pg_class
 ) pg_class
 USING(indexoid);
```

This should give you an output similar to the following when an index is being backfilled.

```tablegen
 indexname | tablet  |     started     |  mem  |  rss
-----------+---------+-----------------+-------+-------
 idx_id    | x'aaaa' | 00:00:04.895382 | 27 MB | 25 MB
```

## Caveats

- In YugabyteDB, the `pg_stat_progress_create_index` view is local; it only has entries for CREATE INDEX commands issued by local YSQL clients.

- In PostgreSQL, `tuples_done` and `tuples_total` refer to the tuples of the _index_. However, in YugabyteDB, these fields refer to the tuples of the _indexed table_. This discrepancy is only observed for partial indexes, where the reported progress is less than the actual progress. `tuples_total` is an estimate that is retrieved from `pg_class.reltuples`.

- In YugabyteDB, `tuples_done` and `tuples_total` are not displayed (set to null) for temporary indexes.

## Learn more

- [Primary keys](../primary-key-ysql/)
- [Secondary indexes](../secondary-indexes-ysql)
- [Optimize query performance](../../../query-1-performance)
