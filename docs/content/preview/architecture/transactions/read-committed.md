---
title: Read Committed isolation level
headerTitle: Read Committed isolation level
linkTitle: Read Committed
description: Details about the Read Committed isolation level
earlyAccess: /preview/releases/versioning/#feature-availability
menu:
  preview:
    identifier: architecture-read-committed
    parent: architecture-acid-transactions
    weight: 50
type: docs
rightNav:
  hideH4: true
---

Read Committed is one of the three isolation levels in PostgreSQL, and also its default. A unique property of this isolation level is that, for transactions running with this isolation, clients do not need to retry or handle serialization errors (40001) in application logic. The other two isolation levels (Serializable and Repeatable Read) require applications to have retry logic for serialization errors. Also, each statement in a read committed transactions works on a new latest snapshot of the database, implying that any data committed before the statement was issued, is visible to the statement.

A read committed transaction in PostgreSQL doesn't raise serialization errors because it internally retries conflicting rows in the statement's execution as of the latest versions of those rows, as soon as conflicting concurrent transactions have finished. This mechanism allows single statements to work on an inconsistent snapshot (in other words, non-conflicting rows are read as of the statement's snapshot, but conflicting rows are re-attempted on the latest version of the row after the conflicting transactions are complete).

YugabyteDB's Read Committed isolation provides slightly stronger guarantees than PostgreSQL's read committed, while providing the same semantics and benefits, that is, a user doesn't have to retry serialization errors in the application logic (modulo [limitation 2](#limitations) around `ysql_output_buffer_size` which is not of relevance for most OLTP workloads). In YugabyteDB, a read committed transaction retries the whole statement instead of retrying only the conflicting rows. This leads to a stronger guarantee where each statement in a YugabyteDB read committed transaction always uses a consistent snapshot of the database, while in PostgreSQL an inconsistent snapshot can be used for statements when conflicts are present. For a detailed example, see [Stronger guarantees in YugabyteDB's read committed isolation](#yugabytedb-s-implementation-with-a-stronger-guarantee).

NOTE: Retries for the statement in YugabyteDB's Read Committed isolation are limited to the per-session YSQL configuration parameter `yb_max_query_layer_retries`. To set it at the cluster level, use the `ysql_pg_conf_csv` TServer gflag. If a serialization error isn't resolved within `yb_max_query_layer_retries`, the error will be returned to the client.

## Implementation and semantics (as in PostgreSQL)

The following two key semantics set apart Read Committed isolation from Repeatable Read in PostgreSQL (refer [Read Committed level](https://www.postgresql.org/docs/13/transaction-iso.html#XACT-READ-COMMITTED)):

1. Each statement should be able to read everything that was committed before the statement was issued. In other words, each statement runs on the latest snapshot of the database as of when the statement was issued.
1. Clients never face serialization errors (40001) in read committed isolation level. To achieve this, PostgreSQL re-evaluates statements for conflicting rows based on a set of rules as described below.

To handle serialization errors in the database without surfacing them to the client, PostgreSQL takes a number of steps based on the statement type.

### UPDATE, DELETE, SELECT FOR [UPDATE, SHARE, NO KEY UPDATE, KEY SHARE]

* If the subject row is being updated or deleted by other concurrent transactions in a conflicting way, wait for the conflicting transactions to commit or rollback, and then perform validation steps.

* If the subject row has been updated or deleted by other concurrent transactions in a conflicting way, perform validation steps.

* If the subject row has been locked by other concurrent transactions in a conflicting way, wait for them to commit or rollback, and then perform validation steps.

Note that two transactions are `concurrent` if their `read time` to `commit time` ranges overlap. If a transaction has not yet committed, the closing range is the current time. Also, for read committed isolation, there is a `read time` for each statement, and not one for the whole transaction.

#### Validation steps

The validation steps are as follows:

1. Read the latest version of the conflicting row and lock it appropriately. The latest version could have a different primary key as well. PostgreSQL finds it by following the chain of updates for a row even across primary key changes. Note that locking is necessary so that another conflict isn't seen on this row while re-evaluating the row again and possibly updating/acquiring a lock on it in step 3. If the locking faces a conflict, it would wait and resume traversing the chain further once unblocked.
1. If the updated version of a row is deleted, ignore it.
1. Apply update, delete, or acquire lock on updated version of the row if the `WHERE` clause evaluates to `true` on the updated version of the row.

### INSERT

* `ON CONFLICT DO UPDATE`: if a conflict occurs, wait for the conflicting transactions to commit or rollback.
  * If all conflicting transactions rollback, proceed as usual.
  * On commit of any conflicting transaction, traverse the chain of updates, as described in validation step 1, and re-evaluate the latest version of the row for any conflict. If there is no conflict, insert the original row. Otherwise, perform the `DO UPDATE` part on the latest version of the row.
* `ON CONFLICT DO NOTHING`: if a conflict occurs, do not do anything.

## YugabyteDB's implementation with a stronger guarantee

Note that the implementation in PostgreSQL (discussed above) can theoretically lead to two different visible semantics:

* A common case which uses an inconsistent snapshot of the database for the same statement's execution.
* A degenerate situation that is highly unlikely to be seen in practice, but is nevertheless possible and provides a stronger guarantee by using a consistent snapshot for the whole statement while still upholding the semantics of Read Committed isolation.

### Common case in PostgreSQL

```sql
CREATE TABLE test (k int primary key, v int);
INSERT INTO test VALUES (2, 5);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (5, 5);
```

```output
INSERT 0 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=10 where k=2;
```

```output
UPDATE 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=100 where v>=5;
```

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
COMMIT;
```

   </td>
   <td>
   </td>
  </tr>

  <tr>
   <td>
   </td>
   <td>

```output
UPDATE 1
```

   </td>
  </tr>

  <tr>
   <td>
   </td>
   <td>

```sql
select * from test;
```

   </td>
  </tr>

<tr>
   <td>
   </td>
   <td>

```output
 k |  v
---+-----
 5 |   5
 2 | 100
(2 rows)
```

   </td>
  </tr>
</tbody>
</table>

As seen above, the UPDATE from transaction 2 first picks the latest snapshot of the database which only has the row (2, 5). The row satisfies the `UPDATE` statement's `WHERE` clause and hence the transaction 2 tries to update the value of `v` from 5 to 100. However, due to an existing conflicting write from transaction 1, it waits for transaction 1 to end. After transaction 1 commits, it re-reads the latest version of only the conflicting row, and re-evaluates the `WHERE` clause. The clause is still satisfied by the new row (2, 10) and so the value is updated to 100. Note that the newly inserted row (5, 5) isn't updated even though it satisfies the `WHERE` clause of transaction 2's `UPDATE`, because it was not part of the snapshot originally picked by transaction 2's `UPDATE` statement.

So, to avoid serialization errors, PostgreSQL only retries the conflicting rows based on their latest versions, thereby allowing a single statement to run on an inconsistent snapshot. In other words, one snapshot is picked for the statement to read all data and process all non-conflicting rows, and a latest version is used for the conflicting rows.

### The unlikely case in PostgreSQL

The other degenerate scenario that can occur differs in the output of the `UPDATE` in transaction 2:

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
update test set v=10 where k=2;
```

```output
UPDATE 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=100 where v>=5;
```

```output
(some processing before snapshot is picked, but feels like postgreSQL is waiting due to a conflict)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
COMMIT;
```

   </td>
   <td>
   </td>
  </tr>

  <tr>
   <td>
   </td>
   <td>

```output
UPDATE 2
```

   </td>
  </tr>

  <tr>
   <td>
   </td>
   <td>

```sql
select * from test;
```

   </td>
  </tr>

<tr>
   <td>
   </td>
   <td>

```output
 k |  v
---+-----
 5 | 100
 2 | 100
(2 rows)
```

   </td>
  </tr>
</tbody>
</table>

The preceding outcome can occur via the following unlikely circumstance: until Client 1 commits, PostgreSQL on Client 2 for some reason is busy or slow, and hasn't yet picked a snapshot for execution. Only after Client 1 commits, transaction on Client 2 picks a snapshot based off the current time for the statement. This leads to both rows being read as part of the snapshot and updated without any observable conflicts.

Both the `common case` and `unlikely` outcomes are valid and satisfy the semantics of Read Committed isolation level. And theoretically, the user cannot figure out which one will be seen because the user cannot differentiate between a pause due to waiting for a conflicting transaction, or a pause due to the database just being busy or slow. Moreover, the `unlikely` case provides a stronger and more intuitive guarantee that the whole statement runs off a single snapshot.

These two possibilities show that the client cannot have application logic that relies on the expectation that the common case occurs always. YugabyteDB implements Read Committed isolation by undoing and retrying a statement whenever serialization errors occur. This provides a stronger guarantee that each statement always works off just a single snapshot, and no inconsistency is allowed even in case of a some conflicting rows. This leads to YugabyteDB always returning output similar to the second outcome in the preceding example which is also simpler to reason.

This might change in future as per [#11573](https://github.com/yugabyte/yugabyte-db/issues/11573), if it gains interest.

## Read restart errors

[Read Restart errors](../read-restart-error) stem from clock skew which is inherent in distributed databases due to the distribution of data across more than one physical node. PostgreSQL doesn't require defining semantics around read restart errors in read committed isolation because it is a single-node database without clock skew.

In general, YugabyteDB has optimizations to resolve this error internally with best-effort before forwarding it to the external client. However, for Read Committed isolation, YugabyteDB gives a stronger guarantee: no `read restart` errors will be thrown to the external client except when a statement's output exceeds `ysql_output_buffer_size` (the size of the output buffer between YSQL and the external client which has a default of 256KB and is configurable). For most OLTP applications, this would hold always as response sizes are usually in this limit.

YugabyteDB chooses to provide this guarantee as most clients that use read committed with PostgreSQL don't have app-level retries for serialization errors. So, it helps to provide the same guarantee for `read restart` errors which are unique to distributed databases.

## Interaction with concurrency control

Read Committed isolation faces the following limitations if using [Fail-on-Conflict](../concurrency-control/#fail-on-conflict) instead of the default [Wait-on-Conflict](../concurrency-control/#wait-on-conflict) concurrency control policy:

* You may have to manually tune the exponential backoff parameters for performance, as described in [Performance tuning](#performance-tuning).
* Deadlock cycles will not be automatically detected and broken quickly. Instead, the `yb_max_query_layer_retries` YSQL configuration parameter will ensure that statements aren't stuck in deadlocks forever.
* There may be unfairness during contention due to the retry-backoff mechanism, resulting in high P99 latencies.

The retries for serialization errors are done at the statement level. Each retry will use a newer snapshot of the database in anticipation that the conflicts might not occur. This is done because if the read time of the new snapshot is higher than the commit time of the earlier conflicting transaction T2, the conflicts with T2 would essentially be voided as T1's statement and T2 would no longer be "concurrent".

## Usage

By setting the YB-TServer flag `yb_enable_read_committed_isolation=true`, the syntactic `Read Committed` isolation in YSQL maps to the Read Committed implementation in DocDB. If set to `false`, it has the earlier behavior of mapping syntactic `Read Committed` on YSQL to Snapshot isolation in DocDB, meaning it behaves as `Repeatable Read`.

The following ways can be used to start a Read Committed transaction after setting the flag:

1. `START TRANSACTION isolation level read committed [read write | read only];`
1. `BEGIN [TRANSACTION] isolation level read committed [read write | read only];`
1. `BEGIN [TRANSACTION]; SET TRANSACTION ISOLATION LEVEL READ COMMITTED;` (this will be supported after [#12494](https://github.com/yugabyte/yugabyte-db/issues/12494) is resolved)
1. `BEGIN [TRANSACTION]; SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED;` (this will be supported after [#12494](https://github.com/yugabyte/yugabyte-db/issues/12494) is resolved)

## Examples

Start by creating the table to be used in all of the examples, as follows:

```sql
CREATE TABLE test (k int primary key, v int);
```

### SELECT behavior without explicit locking

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (1, 5);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v=5;
```

```output
 k | v
---+---
 1 | 5
(1 row)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
insert into test values (2, 5);
```

```output
INSERT 0 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v=5;
```

```output
 k | v
---+---
 1 | 5
(1 row)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (3, 5);
```

```output
INSERT 0 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v=5;
```

```output
 k | v
---+---
 1 | 5
 3 | 5
(2 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v=5;
```

```output
 k | v
---+---
 1 | 5
 2 | 5
 3 | 5
(3 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

### UPDATE behavior

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
insert into test values (5, 5);
```

```output
INSERT 0 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=10 where k=4;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
delete from test where k=3;
```

```output
DELETE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=10 where k=2;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=1 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=10 where k=0;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=100 where v>=5;
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
UPDATE 4
```

```sql
select * from test;
```

```output
 k  |  v
----+-----
  5 | 100
  1 |   1
 10 | 100
  4 | 100
  2 | 100
(5 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

### SELECT FOR UPDATE behavior

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
insert into test values (5, 5);
```

```output
INSERT 0 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=10 where k=4;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
delete from test where k=3;
```

```output
DELETE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=10 where k=2;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set v=1 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=10 where k=0;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v>=5 for update;
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
 k  | v
----+----
  5 |  5
 10 |  5
  4 | 10
  2 | 10
(4 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

### INSERT behavior

Insert a new key that has just been changed by another transaction, as follows:

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (1, 1);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=2 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (2, 1);
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
ERROR:  duplicate key value violates unique constraint "test_pkey"
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
rollback;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

Insert a new key that has just been changed by another transaction, with `ON CONFLICT`:

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (1, 1);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=2 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (2, 1) on conflict (k) do update set v=100;
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
INSERT 0 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test;
```

```output
 k |  v
---+-----
 2 | 100
(1 row)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

Insert an old key that has been removed by another transaction, as follows:

```sql
TRUNCATE TALE test;
INSERT INTO test VALUES (1, 1);
```

<table class="no-alter-colors">
  <thead>
    <tr>
    <th>
    Client 1
    </th>
    <th>
    Client 2
    </th>
    </tr>
  </thead>
  <tbody>
  <tr>
    <td>

```sql
begin transaction isolation level read committed;
```

  </td>
  <td>
  </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=2 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (1, 1);
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
INSERT 0 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test;
```

```output
 k | v
---+---
 1 | 1
 2 | 1
(2 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

Insert an old key that has been removed by another transaction, with `ON CONFLICT`:

```sql
TRUNCATE TABLE test;
INSERT INTO test VALUES (1, 1);
```

<table class="no-alter-colors">
<thead>
  <tr>
   <th>
   Client 1
   </th>
   <th>
   Client 2
   </th>
  </tr>
</thead>
<tbody>
  <tr>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level read committed;
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
update test set k=2 where k=1;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (1, 1) on conflict (k) do update set v=100;
```

```output
(waits)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
commit;
```

   </td>
  </tr>
  <tr>
   <td>

```output
INSERT 0 1
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test;
```

```output
 k |  v
---+-----
 1 |  1
 2 |  1
(2 rows)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

## Cross-feature interaction

Read Committed interacts with the following feature:

* [Follower reads](../../../develop/build-global-apps/follower-reads/): When follower reads is enabled and the transaction block is explicitly marked `READ ONLY`, the read point for each statement in a read committed transaction is selected as `Now()` - `yb_follower_read_staleness_ms`.

## Limitations

* A `SET TRANSACTION ISOLATION LEVEL ...` statement immediately issued after `BEGIN;` or `BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;` will fail if the YB-TServer GFlag `yb_enable_read_committed_isolation=true`, and the following error will be issued:

    ```output
    ERROR:  SET TRANSACTION ISOLATION LEVEL must not be called in a subtransaction
    ```

    For more details, see [#12494](https://github.com/yugabyte/yugabyte-db/issues/12494).

* Read restart and serialization errors are not internally handled in read committed isolation in the following circumstances:
  * the query's response size exceeds the YB-TServer `ysql_output_buffer_size` flag, which has a default value of 256KB (see [#11572](https://github.com/yugabyte/yugabyte-db/issues/11572)).
  * multiple semicolon-separated statements in a single query string are sent via the simple query protocol (see [#21833](https://github.com/yugabyte/yugabyte-db/issues/21833)).
  * for statements other than the first one in a batch sent by the driver (except for [#21607](https://github.com/yugabyte/yugabyte-db/issues/21607) currently).

* Non-transactional side-effects can occur more than once when a `conflict` or `read restart` occurs in functions or procedures in read committed isolation. This is because in read committed isolation, the retry logic in the database will undo all work done as part of that statement and re-attempt the whole client-issued statement. (See [#12958](https://github.com/yugabyte/yugabyte-db/issues/12958))

Read Committed isolation has the following additional limitations when `enable_wait_queues=false` (see [Wait-on-Conflict](../concurrency-control/#wait-on-conflict) and [Interaction with concurrency control](#interaction-with-concurrency-control)):

* You may have to manually tune the exponential backoff parameters for performance, as described in [Performance tuning](#performance-tuning).
* Deadlock cycles will not be automatically detected and broken quickly. Instead, the `yb_max_query_layer_retries` YSQL configuration parameter will ensure that statements aren't stuck in deadlocks forever.
* There may be unfairness during contention due to the retry-backoff mechanism, resulting in high P99 latencies.

## Considerations

This isolation level allows both phantom and non-repeatable reads (as demonstrated in [SELECT behavior without explicit locking](#select-behavior-without-explicit-locking)).

Adding this new isolation level does not affect the performance of existing isolation levels.

### Performance tuning

If a statement in the Read Committed isolation level faces a conflict, it is retried. If using [Fail-on-Conflict](../concurrency-control/#fail-on-conflict) concurrency control mode, the retries are done with exponential backoff until the statement times out or the `yb_max_query_layer_retries` are exhausted, whichever happens first. The following parameters control the backoff:

* `retry_max_backoff` is the maximum backoff in milliseconds between retries.
* `retry_min_backoff` is the minimum backoff in milliseconds between retries.
* `retry_backoff_multiplier` is the multiplier used to calculate the next retry backoff.

You can set these parameters on a per-session basis, or in the `ysql_pg_conf_csv` YB-TServer flag on cluster startup.

If the [Wait-on-Conflict](../concurrency-control/#wait-on-conflict) concurrency control policy is enabled, there won't be a need to manually tune these parameters for performance. Statements will restart only when all conflicting transactions have committed or rolled back, instead of retrying with an exponential backoff.
