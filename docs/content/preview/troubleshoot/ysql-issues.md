---
title: YSQL issues
linkTitle: YSQL issues
menu:
  preview:
    identifier: troubleshoot-ysql
    parent: troubleshoot
    weight: 300
type: docs
rightnav:
    hideh3: true
---

## Connection

### Unable to authenticate after fresh installation

You may encounter the following error when trying to connect to YSQL using the `ysqlsh` CLI after creating a fresh cluster:

```output
ysqlsh: FATAL:  password authentication failed for user "yugabyte"
```

By default, PostgreSQL listens on port `5432`. To avoid conflict, the YSQL port is set to `5433`. But because you can create multiple PostgreSQL clusters locally, each one takes the next port available, starting from `5433`, and thus conflicting with the YSQL port.

If you have created two PostgreSQL clusters before creating the YugabyteDB cluster, the `ysqlsh` shell is trying to connect to PostgreSQL running on port `5433` and fails to authenticate. To verify, you can run the following command to check which process is listening on port `5433`:

```sh
sudo lsof -i :5433
```

```output
COMMAND   PID     USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
postgres 1263 postgres    7u  IPv4  35344      0t0  TCP localhost:postgresql (LISTEN)
```

You can shut down this PostgreSQL cluster or kill the process, and then restart YugabyteDB.

## Databases

### Unable to drop database

When trying to database, you might see an error like:

```sql{.nocopy}
ERROR:  55006: database "test" is being accessed by other users
DETAIL:  There is 1 other session using the database.
```

This is because you cannot drop a database that has active sessions. To list the active sessions, you can use the command:

```sql
select pid, usesysid,usename, application_name, client_addr from pg_stat_activity where datname ='test';
```

Either you can wait till the sessions end or forcefully terminate the session using `pg_terminate_backend` as:

```sql
SELECT pg_terminate_backend(pid) FROM pg_stat_activity
WHERE  pid <> pg_backend_pid() -- dont kill the current connection
    AND datname = 'test'; -- replace with db name
```

You should now be able to drop the database as `DROP DATABASE test;`.

## Collation

### Text ordering is different from PostgreSQL

You might notice in certain cases that the ordering of text is different from PostgreSQL when using the same `ORDER BY` clause. This is probably because the default [collation](../../explore/ysql-language-features/advanced-features/collations/) of your PostgreSQL database is different from your YugabyteDB database. You can ensure the use of same collation in your queries across the two databases, by adding the `collate` clause as:

```sql
select name collate "en_US" from test order by name asc;
```

{{<lead link="">}}
To understand the impact of collations, see [Collations](../../explore/ysql-language-features/advanced-features/collations/)
{{</lead>}}

## DDL

### Catalog Version Mismatch: A DDL occurred while processing this query

When executing queries in the YSQL layer, the query may fail with the following error:

```output
org.postgresql.util.PSQLException: ERROR: Catalog Version Mismatch: A DDL occurred while processing this query. Try Again
```

A DML query in YSQL may touch multiple servers, and each server has a Catalog Version which is used to track schema changes. When a DDL statement runs in the middle of the DML query, the Catalog Version is changed and the query has a mismatch, causing it to fail.

In these cases, the database aborts the query and returns a `40001` PostgreSQL error code. Errors with this code can be safely retried from the client side.

## ysql_dump

### Snapshot too old: When running ysql_dump

When running an `ysql_dump` command that takes too long to complete, you may encounter the following error:

```output
Snapshot too old: Snapshot too old. Read point: { physical: 1628678717824559 }, earliest read time allowed: { physical: 1628679675271006 }, delta (usec): 957446447: kSnapshotTooOld
```

When the command takes a long time to be processed, a compaction may have occurred and have deleted some rows at the snapshot the dump was started on. For large backups, it is recommended to use [distributed snapshots](../../manage/backup-restore/snapshot-ysql/), which are more efficient and fast.

If you really need to use `ysql_dump`, you can increase the [`--timestamp_history_retention_interval_sec`](../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) flag on the master to a higher value. The total time necessary for this command depends on the amount of metadata in your environment, so you might need to tune this flag a couple of times. You can start by setting it to 3600 seconds and iterating from there. Note that, ideally, you don't want to leave this flag at a really high value, as that can have an adverse effect on the runtime of regular metadata queries (for example, DDLs, establishing new connections, and metadata cache refreshes).
