---
title: READ COMMITTED isolation level
headerTitle: READ COMMITTED isolation level
linkTitle: Read Committed
description: Details about READ COMMITTED isolation level
menu:
  latest:
    identifier: architecture-read-committed
    parent: architecture-acid-transactions
    weight: 1153
isTocNested: true
showAsideToc: true
---

## 1 Introduction

READ COMMITTED is one of the three isolation levels in PostgreSQL and also its default. A unique property of this isolation level is: clients don’t need retry logic for **serialization errors (40001)** in applications when using this isolation level.

The other two isolation levels (SERIALIZABLE and REPEATABLE READ) require apps to have retry logic for serialization errors. READ COMMITTED in PostgreSQL works around conflicts by allowing single statements to work on an “_inconsistent snapshot”_ (i.e., non-conflicting rows are read as of the statement’s snapshot but conflict resolution is done by reading and attempting re-execution/ locking on the latest version of the row).

YSQL now supports READ COMMITTED isolation level as well. The behaviour will be the same as that of PostgreSQL’s READ COMMITTED level as mentioned in section 13.2.1 [here](https://www.postgresql.org/docs/13/transaction-iso.html). Notable points from the section are listed again in the Semantics section below.

NOTE: the semantics ensure that the client doesn't face conflict and read restart errors. YSQL maintains these semantics as long as a statement's output doesn't exceed ysql_output_buffer_size (a gflag with a default of 256KB). If this condition is not met, YSQL will resort to optimistic locking for that statement. Work is in progress to get rid of this constraint.

## 2 Semantics

To support READ COMMITTED isolation level in YSQL with the same semantics as PostgreSQL, the following requirements follow -

1. **SELECTs (without explicit row locking)**
    1. New read point is chosen at statement start that includes anything that committed before the query began.
    2. Data from updates by previous statements in same transaction is visible.
2. **UPDATE, DELETE, SELECT FOR UPDATE, FOR SHARE,** _FOR NO KEY UPDATE, FOR KEY SHARE (the last two are not mentioned in PostgreSQL documentation but the same behavior is seen for these as below.)_
    1. New read point is chosen at statement start that includes anything that committed before the query began.
    2. If the row of interest -
        1. is being updated (or deleted) by other transactions in a conflicting way (i.e., read time of statement falls within read time of other transactions to current time), wait for them to commit/rollback and then perform recheck steps (see below).
        2. has been updated (or deleted) by other transactions in a conflicting way (i.e., read time of statement falls within read time to commit time of other transactions), perform recheck steps.
        3. has been locked by other transactions in a conflicting way, wait for them to commit/ rollback and then perform recheck steps.
        <br />
        <br />

        **Recheck steps:**
        1. If a row is deleted, ignore it
        2. Apply update/ acquire lock on updated version of row if where clause evaluates to true on the updated version of row. (Note that the updated version of a row could have a different pk as well - this implies PostgreSQL follows the chain of updates for a row even across pk changes). 
3. **INSERT**
    1. ON CONFLICT DO UPDATE: if a conflict occurs, wait for the conflicting transaction to commit/ rollback.
        1. On rollback, proceed as usual
        2. On commit, modify the new version of row
    2. ON CONFLICT DO NOTHING: do nothing if a conflict occurs

Apart from the above requirements, there is a YSQL specific requirement: ensure that external clients don’t face kReadRestart errors.

## 3 Usage

By setting the gflag `yb_enable_read_committed_isolation=true`, the READ COMMITTED isolation in YSQL will actually map to the READ COMMITTED implementation in docdb. If set to false, it will have the earlier behaviour of mapping READ COMMITTED to REPEATABLE READ.

The following ways can be used to start a READ COMMITTED transaction after setting the gflag:

1. `START TRANSACTION isolation level read committed [read write | read only];`
2. `BEGIN [TRANSACTION] isolation level read committed [read write | read only];`
3. `BEGIN [TRANSACTION]; SET TRANSACTION ISOLATION LEVEL READ COMMITTED;`
4. `BEGIN [TRANSACTION]; SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED;`

READ COMMITTED on YSQL will have pessimistic locking behaviour i.e., a READ COMMITTED transaction
will wait for other READ COMMITTED transactions to commit/ rollback in case of a conflict. Two or
more transactions could be waiting on each other in a cycle. Hence, to avoid a deadlock, make sure
to configure a statement timeout (by setting the `statement_timeout` parameter in `ysql_pg_conf_csv` tserver gflag on cluster startup). Statement timeouts will help avoid deadlocks (see example 1).

## 4 Examples

```sql
create table test (k int primary key, v int);
```

### 1) Deadlocks are avoided in READ COMMITTED by relying on statement timeout.

```sql
truncate table test;
insert into test values (1, 5);
insert into test values (2, 5);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=5 where k=2;
```

```
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

```
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

```
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
</table>


### 2) Behavior of SELECT (without explicit locking)

```sql
truncate table test;
insert into test values (1, 5);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
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

```
INSERT 0 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v=5;
```

```
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

```
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

```
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

```
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
</table>


### 3) Behavior of UPDATE

```sql
truncate table test;
insert into test values (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
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

```
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

```
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

```
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

```
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=100 where v>=5;
```

```
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

```
UPDATE 4
```

```sql
select * from test;
```

```
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
</table>


### 4) Behavior of SELECT FOR UPDATE


```sql
truncate table test;
insert into test values (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
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

```
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

```
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

```
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

```
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where v>=5 for update;
```

```
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
   
```
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
</table>



### 5) Behavior of INSERTs


**i) insert new key that is also just changed by another transaction**


```sql
truncate table test;
insert into test values (1, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (2, 1);
```

```
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

```
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
</table>


**ii) Same as i) but with ON CONFLICT**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>


```sql
insert into test values (2, 1) on conflict (k) do update set v=100;
```

```
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

```
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

```
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
</table>


**iii) INSERT old key that is removed by other transaction**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (1, 1);
```

```
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

```
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

```
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
</table>


**iv) same with iii) but with ON CONFLICT**

```sql
truncate table test;
insert into test values (1, 1);
```

<table>
  <tr>
   <td>
   Client 1
   </td>
   <td>
   Client 2
   </td>
  </tr>
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

```
UPDATE 1
```
   </td>
  </tr>
  <tr>
   <td>

```sql
insert into test values (1, 1) on conflict (k) do update set v=100;
```

```
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

```
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

```
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
</table>

## 5 Cross feature interaction

This feature interacts with the following features:

1. **Follower reads (integration in progress):** When follower reads is turned on, the read point for each statement in a READ COMMITTED transaction will be picked as _Now()_ - _yb_follower_read_staleness_ms_ (if the transaction/statement is known to be explicitly/ implicitly read only).
2. **Pessimistic locking:** READ COMMITTED has a dependency on pessimistic locking to fully work. To be precise, on facing a conflict, a transaction has to wait for the conflicting transaction to rollback/commit. Pessimistic locking behaviour can be seen for READ COMMITTED. An optimized version of pessimistic locking will come in near future, which will give better performance and will also work for REPEATABLE READ and SERIALIZABLE isolation levels. The optimized version will also help detect deadlocks proactively instead of relying on statement timeouts for deadlock avoidance (see example 1).

## 6 Noteworthy Considerations

1. This isolation level allows both phantom and non-repeatable reads (example 2).
2. Adding this new isolation level won’t affect the performance of existing isolation levels.
3. Tuning for performance:
   If a statement in the READ COMMITTED isolation level faces a conflict, it will be retried with
   exponential backoff till the statement times out. There are three parameters that control the
   backoff:
   1. _retry_max_backoff_: the maximum backoff in milliseconds between retries.
   2. _retry_min_backoff_: the minimum backoff in milliseconds between retries.
   3. _retry_backoff_multiplier_: the multiplier used to calculate the next retry backoff.

  These parameters can be set on a per-session basis or in the `ysql_pg_conf_csv` tserver gflag on cluster startup.

  Once the optimized version of pessimistic locking (as described in section 5) is completed, there
  won't be a need to hand tune these parameters for performance. Statements will restart only when
  all conflicting transactions have committed or rolled back (instead of retrying with an
  exponential backoff).