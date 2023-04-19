---
title: View live queries with pg_stat_activity
linkTitle: View live queries
description: Using pg_stat_activity to troubleshoot issues and help to identify long running transactions.
aliases:
headerTitle: View live queries with pg_stat_activity
image: /images/section_icons/index/develop.png
menu:
  preview:
    identifier: pg-stat-activity
    parent: query-tuning
    weight: 300
type: docs
---

YugabyteDB supports the PostgreSQL `pg_stat_activity` view to analyze live queries. This view returns analytic and diagnostic information about active YugabyteDB server processes and queries. The view returns one row per server process, and displays information related to the current status of the database connection.

{{% explore-setup-single %}}

## Supported fields

At a `ysqlsh` prompt, run the following meta-command to return the fields supported by pg_stat_activity:

```sql
yugabyte=# \d pg_stat_activity
```

The following table describes the fields and their values:

| Field | Type | Description |
| :---- | :--- | :---------- |
| `datid` | oid | Object identifier (OID) of the database to which the backend is connected. |
| `datname` | name | Name of the database to which the backend is connected. |
| `pid` | integer | Backend process ID. |
| `usesysid` | oid | The user's OID. |
| `usename` | name | The user's name. |
| `application_name` | text | Name of the application connected to this backend. |
| `client_addr` | inet | The client's IP address. Empty if the client is connected through a Unix socket on the server, or if this is an internal process such as autovacuum. |
| `client_hostname` | text | The client's hostname, as reported by a reverse DNS lookup of client_addr. |
| `client_port` | integer | TCP port the client is using for communication with the backend server. A value of -1 indicates a Unix socket. |
| `backend_start` | timestampz | Time at which the current backend process started. |
| `xact_start` | timestampz | Time at which the current transaction started, or null if no transaction is active. If the current query is the process's first transaction, this field is equivalent to the `query_start` field. |
| `query_start` | timestampz | Time at which the currently active query started. If the state field is not set to active, the query_start field indicates the time when the last query was started. |
| `state_change` | timestampz | Time at which the previous state changed. |
| `wait_event_type` | text | Type of event the backend is waiting for. |
| `wait_event` | text | Name of the event being waited for. |
| `state` | text | Current state of the backend. Valid values are _active_, _idle_, _idle in transaction_, _idle in transaction (aborted)_, _fastpath function call_, and _disabled_. |
| `backend_xid` | xid | This backend's top-level transaction identifier, if any. |
| `backend_xmin` | xid | The current backend's xmin horizon. |
| `query` | text | The last executed query. If `state` is `active`, this is the currently executing query. If `state` has a different value, this is the last executed query. By default, the query text is limited to the first 1,024 characters. Adjust the `track_activity_query_size` parameter to change the character limit. |
| `backend_type` | text | The current backend's type. Possible values are _autovacuum launcher_, _autovacuum worker_, _background worker_, _background writer_, _client backend_, _checkpointer_, _startup_, _walreceiver_, _walsender_, and _walwriter_. |
| `allocated_mem_bytes` | bigint | Heap memory usage in bytes of the backend process. |
| `rss_mem_bytes` | bigint | Resident Set Size of the backend process in bytes. It shows how much memory is allocated to the process and is in RAM. It does not include memory that is swapped out. |

## Examples

### Get basic information

The following query returns basic information about active Yugabyte processes:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query
    FROM pg_stat_activity;
```

```output
 datname  |  pid  | application_name | state  |                                   query
----------+-------+------------------+--------+----------------------------------------------------------------------------
 yugabyte | 10027 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
          | 10013 |                  |        |
(2 rows)
```

In this listing:

- `datname` is the database connected to this process.
- `pid` is the process ID.
- `application_name` is the application connected to this process.
- `state` is the operational condition of the process.
- `query` is the latest query executed for this process.

### Identify and terminate an open transaction

Often enough, you may need to identify long-running queries, because these queries could indicate deeper problems. The pg_stat_activity view can help identify these issues. In this example, you create an open transaction, identify it, and terminate it. The example uses the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/).

#### Create an open transaction

1. Use the following query to return a row from the users table.

    ```sql
    yb_demo=# SELECT id, name, state
        FROM users
        WHERE id = 212;
    ```

    ```output
     id  |     name      | state
    -----+---------------+-------
     212 | Jacinthe Rowe | CO
    (1 row)
    ```

1. Update the state column value of this role with a transaction. The query is deliberately missing the `END;` statement to close the transaction.

    ```sql
    yb_demo=# BEGIN TRANSACTION;
        UPDATE users
            SET state = 'IA'
            WHERE id = 212;
    ```

    ```output
    BEGIN
    UPDATE 1
    ```

#### Find the open transaction

Because the transaction never ends, it wastes resources as an open process.

1. Check the state of the transaction by opening another `ysqlsh` instance and finding information about this idle transaction with pg_stat_activity.

    ```sql
    yugabyte=# SELECT datname, pid, application_name, state, query
        FROM pg_stat_activity;
    ```

    ```output
     datname  |  pid  | application_name |        state        |                                   query
    ----------+-------+------------------+---------------------+----------------------------------------------------------------------------
     yugabyte | 10381 | ysqlsh           | active              | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
     yb_demo  | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212;
              | 10013 |                  |                     |
    (3 rows)
    ```

1. Find the idle transaction's PID. In the sample output in the previous step, it's PID 10033, in the second row.

#### Terminate the open transaction

1. Terminate the idle transaction. Replace `<pid>` with the PID of the process to terminate.

    ```sql
    yugabyte=# SELECT pg_terminate_backend(<pid>);
    ```

    ```output
     pg_terminate_backend
    ---------------------------
     t
    (1 row)
    ```

1. The pg_terminate_backend function returns `t` on success, and `f` on failure. Query pg_stat_activity again in the second terminal, and verify that the idle process has ended.

    ```sql
    yugabyte=# SELECT datname, pid, application_name, state, query
        FROM pg_stat_activity;
    ```

    ```output
     datname  |  pid  | application_name | state  |                                   query
    ----------+-------+------------------+--------+----------------------------------------------------------------------------
     yugabyte | 10381 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
              | 10013 |                  |        |
    (2 rows)
    ```

### Other time-related queries

You can run some time-related queries to help you identify long-running transactions. These are particularly helpful when there are a lot of open connections on that node.

**Get a list of processes ordered by current `txn_duration`**:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query, now() - xact_start
    AS txn_duration
    FROM pg_stat_activity
    ORDER BY txn_duration desc;
```

```output
 datname  |  pid  | application_name | state  |                                  query                                  | txn_duration
----------+-------+------------------+--------+-------------------------------------------------------------------------+--------------
 yugabyte | 17695 | ysqlsh           | idle   |                                                                         |
          | 17519 |                  |        |                                                                         |
 yugabyte | 17540 | ysqlsh           | active | SELECT datname, pid, application_name, state, query, now() - xact_start+| 00:00:00
          |       |                  |        |     AS txn_duration                                                    +|
          |       |                  |        |     FROM pg_stat_activity                                              +|
          |       |                  |        |     ORDER BY txn_duration desc;                                         |
(3 rows)
```

**Get a list of processes where the current transaction has taken more than 1 minute**:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query, xact_start
    FROM pg_stat_activity
    WHERE now() - xact_start > '1 min';
```

```output
 datname |  pid  | application_name |        state        |                     query                     |          xact_start
---------+-------+------------------+---------------------+-----------------------------------------------+------------------------------
 yb_demo | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212; | 2021-05-06 15:26:28.74615-04
(1 row)
```

## Learn more

- Refer to [Get query statistics using pg_stat_statements](../pg-stat-statements/) to track planning and execution of all the SQL statements.
- Refer to [View COPY progress with pg_stat_progress_copy](../pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
- Refer to [Optimize YSQL queries using pg_hint_plan](../pg-hint-plan/) show the query execution plan generated by YSQL.
