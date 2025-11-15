---
title: Time travel queries
headerTitle: Time travel query
linkTitle: Time travel query
description: Read data at a specific point in time for data recovery and analysis.
tags:
  feature: tech-preview
menu:
  stable:
    identifier: time-travel-query
    parent: backup-restore
    weight: 750
type: docs
---

Use time travel queries to read data as it was at a specific point in time, within a configurable retention period. This includes reading data that has been changed or deleted. Use time travel queries for the following:

- Read rows that have been deleted by mistake. Restore the rows by exporting the result of the query and then importing it back into the database.
- Analyze trends and data changes over time.

## Configure time travel queries

### Set the history retention interval

The history retention period (that is, the period available for historical queries) is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). This is a cluster-wide global flag that affects every YSQL database and YCQL keyspace.

In addition, you must also set the `timestamp_syscatalog_history_retention_interval_sec` flag to cover the time interval you want to query.

For example, to be able to query the data as of the last 24 hours (86000 seconds), set both flags to 86000.

The default retention period is 900 seconds (15 minutes).

### Set the read time

To enable time travel queries, set the `yb_read_time` YSQL configuration parameter to specify the timestamp at which you want to read your queries. `yb_read_time` takes a Unix timestamp in microseconds, which allows you to read data at up to microsecond precision. After setting the parameter, all subsequent read queries are executed as of that read time, in the current session.

Suppose the current point in time is `Mar-13-2025 13:00:00`, and you want to read the data as of timestamp `Mar-13-2025 09:48:46` (which corresponds to Unix timestamp `1741909726000000`). Set the read time as follows:

```sql
SET yb_read_time TO 1741909726000000;
```

All subsequent queries in the session will read data as of `Mar-13-2025 09:48:46`.

When setting `yb_read_time`, keep in mind the following:

- `yb_read_time` is defined on a YSQL session level. This means that all the read queries in the current session will read the data as of `yb_read_time`. Other YSQL sessions are not affected.
- To reset the session to normal behavior (current time), set `yb_read_time` to 0.
- Write DML queries (INSERT, UPDATE, DELETE) and DDL queries are not allowed in a session that has a read time in the past.
- Currently, time travel queries can only read old data without schema changes. In other words, do not set the read time to a time earlier than the most recent DDL operation.

## Example

The following example shows how you can use time travel queries to recover accidentally deleted rows from a table.

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

1. Determine the exact time when your database is in the correct state. You will use this timestamp as the read timestamp for the time travel query. Use the following query to retrieve the current time in Unix timestamp format:

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

In cases where the deletion affected many tables in the database, you can use time travel queries to read the deleted rows for every table. Alternatively, you can use [instant database cloning](../instant-db-cloning/) to create a zero-copy, independent writable clone of your database as of a timestamp in the past.
