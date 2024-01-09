---
title: Read Committed isolation level
headerTitle: Read Committed isolation level
linkTitle: Read Committed
description: Details about the Read Committed isolation level
techPreview: /preview/releases/versioning/#feature-availability
menu:
  v2.12:
    identifier: architecture-read-committed
    parent: architecture-acid-transactions
    weight: 1153
type: docs
---

Read Committed is one of the three isolation levels in PostgreSQL, and also its default. A unique property of this isolation level is that clients don't need retry logic for serialization errors (40001) in applications when using this isolation level.

The other two isolation levels (Serializable and Repeatable Read) require apps to have retry logic for serialization errors. Read Committed in PostgreSQL works around conflicts by allowing single statements to work on an _inconsistent snapshot_ (in other words, non-conflicting rows are read as of the statement's snapshot, but conflict resolution is done by reading and attempting re-execution/ locking on the latest version of the row).

YSQL supports the Read Committed isolation level, and its behavior is the same as that of PostgreSQL's [Read Committed level (section 13.2.1)](https://www.postgresql.org/docs/13/transaction-iso.html#XACT-READ-COMMITTED).

## Semantics

{{< note title="YSQL requirement" >}}

In addition to the requirements that follow, there is another YSQL specific requirement: ensure that external clients don't face `kReadRestart` errors.

{{</note>}}

To support the Read Committed isolation level in YSQL with the same semantics as PostgreSQL, the following requirements apply:

### SELECT (without explicit row locking)

1. New read point is chosen at statement start that includes anything that committed before the query began.
1. Data from updates by previous statements in the same transaction is visible.

### UPDATE, DELETE, SELECT FOR UPDATE, FOR SHARE, FOR NO KEY UPDATE, FOR KEY SHARE

(The last two are not mentioned in the PostgreSQL documentation, but the same behavior is seen for these as below.)

1. New read point is chosen at statement start that includes anything that committed before the query began.
1. If the row of interest:
    * is being updated (or deleted) by other transactions in a conflicting way (the statement's read time falls within the read time of other transactions to current time), wait for them to commit or rollback, then perform recheck steps (see below).
    * has been updated (or deleted) by other transactions in a conflicting way (a statement's read time falls within the read time to commit time of other transactions), perform recheck steps.
    * has been locked by other transactions in a conflicting way, wait for them to commit or rollback, then perform recheck steps.

The **recheck steps** are as follows:

1. If a row is deleted, ignore it.
1. Apply update/acquire lock on updated version of row if where clause evaluates to true on the updated version of row. (Note that the updated version of a row could have a different pk as well; this implies that PostgreSQL follows the chain of updates for a row even across primary key changes).

### INSERT

1. ON CONFLICT DO UPDATE: if a conflict occurs, wait for the conflicting transaction to commit or rollback.
    1. On rollback, proceed as usual.
    1. On commit, modify the new version of row.
1. ON CONFLICT DO NOTHING: do nothing if a conflict occurs.

## Usage

By setting the tserver gflag `yb_enable_read_committed_isolation=true`, the Read Committed isolation in YSQL will actually map to the Read Committed implementation in DocDB. If set to false, it will have the earlier behavior of mapping Read Committed to REPEATABLE READ.

The following ways can be used to start a Read Committed transaction after setting the gflag:

1. `START TRANSACTION isolation level read committed [read write | read only];`
2. `BEGIN [TRANSACTION] isolation level read committed [read write | read only];`
3. `BEGIN [TRANSACTION]; SET TRANSACTION ISOLATION LEVEL READ COMMITTED;`
4. `BEGIN [TRANSACTION]; SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED;`

Read Committed on YSQL will have pessimistic locking behavior; in other words, a Read Committed transaction will wait for other Read Committed transactions to commit or rollback in case of a conflict. Two or more transactions could be waiting on each other in a cycle. Hence, to avoid a deadlock, be sure to configure a statement timeout (by setting the `statement_timeout` parameter in `ysql_pg_conf_csv` tserver gflag on cluster startup). Statement timeouts will help avoid deadlocks (see the [first example](#avoid-deadlocks-in-read-committed-transactions)).

## Examples

Start by setting up the table you'll use in all of the examples in this section.

```sql
create table test (k int primary key, v int);
```

### Avoid deadlocks in Read Committed transactions

You can avoid deadlocks in Read Committed transactions by relying on statement timeout.

```sql
truncate table test;
insert into test values (1, 5);
insert into test values (2, 5);
```

<table>
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
set statement_timeout=2000;
```

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=5 where k=1;
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
update test set v=5 where k=2;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=5 where k=2;
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
update test set v=5 where k=1;
```

```output
ERROR:  cancelling statement due to statement timeout
```

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
rollback;
```

   </td>
  </tr>
  <tr>
   <td>

```output
UPDATE 1
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

### SELECT behavior without explicit locking

```sql
truncate table test;
insert into test values (1, 5);
```

<table>
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
truncate table test;
insert into test values (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table>
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
truncate table test;
insert into test values (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table>
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

**Insert a new key that is also just changed by another transaction**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
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

**Same as previous, but with ON CONFLICT**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
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

**INSERT old key that is removed by other transaction**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
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

**Same as previous, but with ON CONFLICT**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
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

## Cross feature interaction

This feature interacts with the following features:

1. **Follower reads (integration in progress):** When follower reads is turned on, the read point for each statement in a Read Committed transaction will be picked as _Now()_ - _yb_follower_read_staleness_ms_ (if the transaction/statement is known to be explicitly/ implicitly read only).

2. **Pessimistic locking:** Read Committed has a dependency on pessimistic locking to fully work. To be precise, on facing a conflict, a transaction has to wait for the conflicting transaction to rollback/commit. Pessimistic locking behavior can be seen for Read Committed. An optimized version of pessimistic locking will come in near future, which will give better performance and will also work for REPEATABLE READ and SERIALIZABLE isolation levels. The optimized version will also help detect deadlocks proactively instead of relying on statement timeouts for deadlock avoidance (see example 1).

## Limitations

Work is in progress to remove these limitations:

1. Read Committed semantics ensure that the client doesn't face conflict and read restart errors. YSQL maintains these semantics as long as a statement's output doesn't exceed `ysql_output_buffer_size` (a gflag with a default of 256KB). If this condition is not met, YSQL will resort to optimistic locking for that statement.

1. PostgreSQL requires the following [as mentioned in its docs](https://www.postgresql.org/docs/current/xfunc-volatility.html): "STABLE and IMMUTABLE functions use a snapshot established as of the start of the calling query, whereas VOLATILE functions obtain a fresh snapshot at the start of each query they execute." YSQL uses a single snapshot for the whole procedure instead of one for each statement in the procedure.

## Noteworthy Considerations

This isolation level allows both phantom and non-repeatable reads (as in [this example](#select-behavior-without-explicit-locking)).

Adding this new isolation level doesn't affect the performance of existing isolation levels.

### Performance tuning

If a statement in the Read Committed isolation level faces a conflict, it will be retried with
exponential backoff till the statement times out. The following parameters control the
backoff:

* _retry_max_backoff_ is the maximum backoff in milliseconds between retries.
* _retry_min_backoff_ is the minimum backoff in milliseconds between retries.
* _retry_backoff_multiplier_ is the multiplier used to calculate the next retry backoff.

You can set these parameters on a per-session basis, or in the `ysql_pg_conf_csv` tserver gflag on cluster startup.

After the optimized version of pessimistic locking (as described in [Cross-feature interaction](#cross-feature-interaction)) is completed, there won't be a need to hand tune these parameters for performance. Statements will restart only when all conflicting transactions have committed or rolled back, instead of retrying with an exponential backoff.
