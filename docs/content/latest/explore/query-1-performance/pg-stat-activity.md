---
title: View Live Queries with pg_stat_activity
linkTitle: View Live Queries with pg_stat_activity
description: Using pg_stat_activity to troubleshoot issues and help to identify long running transactions.
aliases:
headerTitle: View Live Queries with pg_stat_activity
image: /images/section_icons/index/develop.png
menu:
  latest:
    identifier: pg-stat-activity
    parent: query-tuning
    weight: 566
isTocNested: true
showAsideToc: true
---

Yugabyte uses PostgreSQL’s `pg_stat_activity` view to analyze live queries.
This view returns analytic and diagnostic information about active Yugabyte server processes and queries.
The pg_stat_activity view returns one row per server process, and displays information related to the current status of the database connection.

At a `ysqlsh` prompt, run the following command to return the columns supported by pg_stat_activity:

```sql
yugabyte=# \d pg_stat_activity
```

```output
                     View "pg_catalog.pg_stat_activity"
     Column      |           Type           | Collation | Nullable | Default
------------------+--------------------------+-----------+----------+---------
datid            | oid                      |           |          |
datname          | name                     |           |          |
pid              | integer                  |           |          |
usesysid         | oid                      |           |          |
usename          | name                     |           |          |
application_name | text                     |           |          |
client_addr      | inet                     |           |          |
client_hostname  | text                     |           |          |
client_port      | integer                  |           |          |
backend_start    | timestamp with time zone |           |          |
xact_start       | timestamp with time zone |           |          |
query_start      | timestamp with time zone |           |          |
state_change     | timestamp with time zone |           |          |
wait_event_type  | text                     |           |          |
wait_event       | text                     |           |          |
state            | text                     |           |          |
backend_xid      | xid                      |           |          |
backend_xmin     | xid                      |           |          |
query            | text                     |           |          |
backend_type     | text                     |           |          |
```

### Examples

#### Get basic information

The following query returns basic information about active Yugabyte processes.

```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```

```output
datname  |  pid  | application_name | state  |                                   query                                   
----------+-------+------------------+--------+----------------------------------------------------------------------------
yugabyte | 10027 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
         | 10013 |                  |        |
(2 rows)
```

Where:

`datname` is the database connected to this process.                               

`pid` is the ID value of the specific process.                                      

`application_name` is the application connected to this process.                                

`state` is the operational condition of the process. State can be one of the following:

* active
* idle
* idle in transaction
* idle in transaction (aborted)
* fastpath function call
* disabled

`query` is the latest query executed for this process.

#### Identify and terminate an open transaction

In the next example, you identify an open transaction. Follow the steps on [this page](https://download.yugabyte.com/#macos) to load a sample dataset.

Often enough, you may need to identify long-running queries, because these queries could indicate deeper problems. The pg_stat_activity view can help identify these issues. In this example, you'll use the users table in the sample `yb_demo` database.

This query returns a row from the users table:

```sql
yugabyte=# SELECT id, name, state FROM users WHERE id = 212;
```

```output
id  |     name      | state
-----+---------------+-------
212 | Jacinthe Rowe | CO
(1 row)
```

You might want to update the state column value of this role with a transaction, as shown in this query:

```sql
yugabyte=# BEGIN TRANSACTION; UPDATE users SET state = 'IA' WHERE id = 212;
```

However, you forgot to add `END;` at the end of the query, to close the transaction. So, the above query will return

```output
BEGIN
UPDATE 1
```

But since the transaction never ended, it wastes resources as an open process.
You can check for the state of the transaction by opening another `ysqlsh` instance and finding information about this idle transaction with pg_stat_activity:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```

```output
datname  |  pid  | application_name |        state        |                                   query                                   
----------+-------+------------------+---------------------+----------------------------------------------------------------------------
yugabyte | 10381 | ysqlsh           | active              | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
yb_demo  | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212;
         | 10013 |                  |                     |
(3 rows)
```

The second row's state column shows the idle transaction with a pid of 10033.
Additionally, you can run some time-related queries to help you identify long-running transactions. These are particularly useful when there are a lot of open connections on that node.

Get a list of processes ordered by current `txn_duration`:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query, now() - xact_start AS txn_duration
FROM pg_stat_activity ORDER BY txn_duration desc;
```

```output
datname  |  pid  | application_name |        state        |                                          query                                          |  txn_duration  
----------+-------+------------------+---------------------+-----------------------------------------------------------------------------------------+-----------------
         | 10013 |                  |                     |                                                                                         |
yb_demo  | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212;                                           | 00:03:47.957187
yugabyte | 10381 | ysqlsh           | active              | SELECT datname, pid, application_name, state, query, now() - xact_start AS txn_duration+| 00:00:00
         |       |                  |                     | FROM pg_stat_activity ORDER BY txn_duration desc;                                       |
(3 rows)
```

Or, get a list of processes where the current transaction has taken more than 1 minute:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query, xact_start FROM pg_stat_activity WHERE now() - xact_start > '1 min';
```

```output
datname |  pid  | application_name |        state        |                     query                     |          xact_start         
---------+-------+------------------+---------------------+-----------------------------------------------+------------------------------
yb_demo | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212; | 2021-05-06 15:26:28.74615-04
(1 row)
```

Finally, terminate the idle transaction (pid 10033):

```sql
yugabyte=# SELECT pg_terminate_backend(10033);
```

```output
pg_terminate_backend
---------------------------
t
(1 row)
```

The pg_terminate_backend function returns `t` on success, and `f` on failure. Query pg_stat_activity again in the second terminal, and you see that the idle process has ended:

```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```

```output
datname  |  pid  | application_name | state  |                                   query                                   
----------+-------+------------------+--------+----------------------------------------------------------------------------
yugabyte | 10381 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
         | 10013 |                  |        |
(2 rows)
```
