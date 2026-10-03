---
title: Inspect at a point in time
headerTitle: Inspect at PIT
linkTitle: Inspect at PIT
description: Read data at a specific point in time for data recovery and analysis.
headcontent: Query data as it was at a specific point in time
aliases:
  - /stable/manage/backup-restore/time-travel-query/
menu:
  stable:
    identifier: pitr-inspect
    parent: point-in-time-recovery
    weight: 20
type: docs
---

{{< tip title="Which PITR method should I use?" >}}
To decide which point-in-time feature is right for your use case, refer to [The PIT recovery family](../#the-pit-recovery-family).
{{< /tip >}}

Inspect at PIT (also known as time travel queries) lets you read data as it was at a specific point in time, within a configurable retention period. This includes reading data that has been changed or deleted. Use Inspect at PIT for the following:

- Determine whether and what intervening data has been written since an error, so you can choose among [Clone](../clone/), [Rewind](../rewind/), or [Restore](../restore/).
- Read rows that have been deleted by mistake. Restore the rows by exporting the result of the query and then importing it back into the database.
- After a restore, repeatedly query at various points in time during the search phase of a surgical recovery.
- Analyze trends and data changes over time.

Inspect runs on the original database (read-only as of the chosen time) and does not create a separate copy. It currently cannot cross DDL boundaries: do not set the read time earlier than the most recent DDL on the objects you query.

## Configure Inspect at PIT

### Set the history retention interval

The history retention period (that is, the period available for historical queries) is controlled by the [history retention interval flag](../../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). This is a cluster-wide global flag that affects every YSQL database and YCQL keyspace.

You should set the `timestamp_syscatalog_history_retention_interval_sec` flag to cover the time interval you want to query. You may also need to increase the history retention period if you are executing a long-running query in an Inspect session.

For example, to be able to query the data as of the last 24 hours (86400 seconds), set both flags to 86400.

The default retention period is 900 seconds (15 minutes).

If a long-running query fails (for example, with a Snapshot too old error because its execution time exceeded the retention window), increase the history retention period (by setting both flags) and re-run the query. Note that increasing the retention period cannot recover history that has already been compacted.

### Set the read time

To enable Inspect at PIT, set the `yb_read_time` YSQL configuration parameter to specify the timestamp at which you want to read your queries. `yb_read_time` takes a Unix timestamp in microseconds, which allows you to read data at up to microsecond precision. After setting the parameter, all subsequent read queries are executed as of that read time, in the current session.

Suppose the current point in time is `Mar-13-2025 13:00:00`, and you want to read the data as of timestamp `Mar-13-2025 09:48:46` (which corresponds to Unix timestamp `1741909726000000`). Set the read time as follows:

```sql
SET yb_read_time TO 1741909726000000;
```

All subsequent queries in the session will read data as of `Mar-13-2025 09:48:46`.

When setting `yb_read_time`, keep in mind the following:

- `yb_read_time` is defined on a YSQL session level. This means that all the read queries in the current session will read the data as of `yb_read_time`. Other YSQL sessions are not affected.
- To reset the session to normal behavior (current time), set `yb_read_time` to 0.
- Write DML queries (INSERT, UPDATE, DELETE) and DDL queries are not allowed in a session that has a read time in the past.
- Currently, Inspect at PIT can only read old data without schema changes. In other words, do not set the read time to a time earlier than the most recent DDL operation. This includes cluster-wide DDLs that affect global objects, such as a clone operation.

## Example

The following example shows how you can use Inspect at PIT to recover accidentally deleted rows from a table.

1. Create a basic table with 10 rows and insert data as follows:

    ```sql
    CREATE TABLE t(k int primary key, v int);
    INSERT INTO t SELECT i, 2*i FROM generate_series(1,10) AS i;
    ```

    ```sql
    SELECT * FROM t ORDER BY k;
    ```

    ```output
    k  | v
    ----+----
    1 |  2
    2 |  4
    3 |  6
    4 |  8
    5 | 10
    6 | 12
    7 | 14
    8 | 16
    9 | 18
    10 | 20
    (10 rows)
    ```

1. Determine the exact time when your database is in the correct state. You will use this timestamp as the read timestamp. Use the following query to retrieve the current time in Unix timestamp format:

    ```sql
    SELECT (EXTRACT (EPOCH FROM CURRENT_TIMESTAMP)*1000000)::decimal(38,0);
    ```

    ```output
        numeric
    ------------------
    1741886500266607
    (1 row)
    ```

1. To simulate user error, delete the last 5 rows.

    ```sql
    DELETE FROM t WHERE k >5;
    ```

    ```sql
    SELECT * FROM t ORDER BY k;
    ```

    ```output
    k | v
    ---+----
    1 |  2
    2 |  4
    3 |  6
    4 |  8
    5 | 10
    (5 rows)
    ```

1. To recover the deleted rows, set the `yb_read_time` parameter to the timestamp you collected.

    ```sql
    SET yb_read_time TO 1741886500266607;
    ```

    ```output
    NOTICE:  yb_read_time should only be set for read-only queries. Write-DML or DDL queries are not allowed when yb_read_time is set.
    SET
    ```

    ```sql
    SELECT * FROM t ORDER BY k;
    ```

    ```output
    k  | v
    ----+----
    1 |  2
    2 |  4
    3 |  6
    4 |  8
    5 | 10
    6 | 12
    7 | 14
    8 | 16
    9 | 18
    10 | 20
    (10 rows)
    ```

    Now that you can read the historical data as of the specified timestamp, you can do forensic analysis and export the mistakenly dropped rows into an external file, and then insert them back in a normal session.

1. Export the last 5 rows using the COPY command:

    ```sql
    COPY (SELECT * FROM t WHERE k>5 ORDER BY k) TO '~/share/exported_table.csv' DELIMITER ',' CSV HEADER;
    ```

    ```output
    COPY 5
    ```

1. Insert the exported rows using the COPY command. You can do this in a new ysqlsh session, or by resetting `yb_read_time` to 0 in the same session.

    ```sql
    SET yb_read_time TO 0;
    ```

    ```sql
    COPY t(k, v) FROM '~/share/exported_table.csv' DELIMITER ',' CSV HEADER;
    ```

    ```sql
    SELECT * FROM t ORDER BY k;
    ```

    ```output
    k  | v
    ----+----
    1 |  2
    2 |  4
    3 |  6
    4 |  8
    5 | 10
    6 | 12
    7 | 14
    8 | 16
    9 | 18
    10 | 20
    (10 rows)
    ```

In cases where the deletion affected many tables in the database, you can use Inspect at PIT to read the deleted rows for every table. Alternatively, you can use [Clone to PIT](../clone/) to create a zero-copy, independent writable clone of your database as of a timestamp in the past.

## Limitations

- Inspect at PIT is not supported for temporary tables.
- You cannot inspect a time prior to the creation time of a database clone.
- Inspect at PIT currently does not support [vector indexes](../../../../additional-features/pg-extensions/extension-pgvector/#vector-indexing). {{<issue 20829>}}
- You cannot query across a DDL boundary (for example, if a table was altered at time t1, you cannot query that table as of a time before t1).
