---
title: YSQL issues
linkTitle: YSQL issues
menu:
  stable:
    identifier: troubleshoot-ysql
    parent: troubleshoot
    weight: 300
type: docs
rightnav:
    hideh3: true
---

## Connection

### Unable to authenticate after fresh installation

You may encounter the following error when trying to connect to YSQL using the ysqlsh CLI after creating a fresh cluster:

```output
ysqlsh: FATAL:  password authentication failed for user "yugabyte"
```

By default, PostgreSQL listens on port `5432`. To avoid conflict, the YSQL port is set to `5433`. But because you can create multiple PostgreSQL clusters locally, each one takes the next port available, starting from `5433`, and thus conflicting with the YSQL port.

If you have created two PostgreSQL clusters before creating the YugabyteDB cluster, the ysqlsh shell is trying to connect to PostgreSQL running on port `5433` and fails to authenticate. To verify, you can run the following command to check which process is listening on port `5433`:

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

### Catalog version mismatch: A DDL occurred while processing this query

When executing queries in the YSQL layer, a query may fail with an error similar to the following:

```output
ERROR:  The catalog snapshot used for this transaction has been invalidated: expected: 15552, got: 15550: MISMATCHED_SCHEMA
```

This means a breaking DDL (a schema or cluster-wide change, such as `ALTER ROLE` or `REVOKE`) committed on another connection while this query was running. The database aborts the query and returns SQLSTATE [40001](../../develop/learn/transactions/transactions-errorcodes-ysql/#40001-serialization-failure), which is always safe to retry.

YSQL already attempts to [retry](../../develop/learn/transactions/transactions-retries-ysql/#automatic-retries) some `40001` errors, including this one, transparently on your behalf for plain DML (`SELECT`/`INSERT`/`UPDATE`/`DELETE`) statements. DDL statements are not retried automatically. If you still see this error, retry the statement or transaction from the client.

Most common database-level DDL, such as `CREATE TABLE`, `ALTER TABLE`, or `DROP TABLE`, do not cause this error on their own, because they are not usually breaking DDL (DDL event triggers can sometimes cause exceptions to this rule). It's typically caused by a change with cluster-wide effect, such as a role or tablespace change.

To find which statement caused the mismatch, check the PostgreSQL log on the node that ran the DDL for a line similar to:

```output
LOG:  MaybeLogNewSQLIncrementCatalogVersion: incrementing all master db catalog versions (breaking) with inval messages, new version for database 13537 is 2
DETAIL:  Local version: 1, node tag: ALTER ROLE.
```

The `node tag` names the statement that produced the new version.

## ysql_dump

### Snapshot too old: When running ysql_dump

When running an `ysql_dump` command that takes too long to complete, you may encounter the following error:

```output
Snapshot too old: Snapshot too old. Read point: { physical: 1628678717824559 }, earliest read time allowed: { physical: 1628679675271006 }, delta (usec): 957446447. Tablet: 245a715c23854b4d8a79df4774659fab, Table: example (000034d9000030008000000000004008): kSnapshotTooOld
```

When the command takes a long time to be processed, a compaction may have occurred and have deleted some rows at the snapshot the dump was started on. For large backups, it is recommended to use [distributed snapshots](../../manage/backup-restore/snapshot-ysql/), which are more efficient and fast.

If you really need to use `ysql_dump`, you can increase the [`--timestamp_history_retention_interval_sec`](../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) flag on the master to a higher value. The total time necessary for this command depends on the amount of metadata in your environment, so you might need to tune this flag a couple of times. You can start by setting it to 3600 seconds and iterating from there. Note that, ideally, you don't want to leave this flag at a really high value, as that can have an adverse effect on the runtime of regular metadata queries (for example, DDLs, establishing new connections, and metadata cache refreshes).
