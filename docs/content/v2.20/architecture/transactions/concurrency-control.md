---
title: Concurrency control
headerTitle: Concurrency control
linkTitle: Concurrency control
description: Details about Concurrency control in YSQL
menu:
  v2.20:
    identifier: architecture-concurrency-control
    parent: architecture-acid-transactions
    weight: 30
type: docs
---

[Concurrency control](https://en.wikipedia.org/wiki/Concurrency_control) in databases ensures that multiple transactions can execute concurrently while preserving data integrity. Concurrency control is essential for correctness in environments where two or more transactions can access the same data at the same time.

YugabyteDB provides two policies to handle conflicts between concurrent transactions as described in the following sections.

For information on how row-level explicit locking clauses behave with these concurrency control policies, refer to [Row-level explicit locking clauses](#row-level-explicit-locking-clauses).

## Fail-on-Conflict

This is the default concurrency control strategy and is applicable for `Repeatable Read` and `Serializable` isolation levels.
It is not applicable for [Read Committed](../read-committed/) isolation.

In this mode, transactions are assigned random priorities with some exceptions as described in [Transaction Priorities](../transaction-priorities/).

If a conflict occurs, a transaction with the lower priority is aborted. There are two possibilities when a transaction T1 tries to read, write, or lock a row in a mode conflicting with other concurrent transactions:

- **Wound:** If T1 has a higher priority than all the other conflicting transactions, T1 will abort them and make progress.
- **Die:** If any other conflicting transaction has an equal or higher priority than T1, T1 will abort itself.

Suppose you have a table with some data in it. The following examples describe the wound and die approaches when a conflict occurs.

```sql
create table test (k int primary key, v int);
insert into test values (1, 1);
```

### Wound example

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
   </td>
   <td>

```sql
SET yb_transaction_priority_upper_bound = 0.4;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
SET yb_transaction_priority_lower_bound = 0.6;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
 k | v
---+---
 1 | 1
(1 row)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
 k | v
---+---
 1 | 1
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
select * from test;
```

```output
ERROR:  Operation expired: Heartbeat:
Transaction 13fb5a0a-012d-4821-ae1d-5f7780636dd4 expired
or aborted by a conflict: 40001
```

```sql
rollback;
```

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

### Die example

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
   </td>
   <td>

```sql
SET yb_transaction_priority_lower_bound = 0.6;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
SET yb_transaction_priority_upper_bound = 0.4;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
 k | v
---+---
 1 | 1
(1 row)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
ERROR:  All transparent retries exhausted. could not serialize
access due to concurrent update
```

```sql
rollback;
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
</tbody>
</table>

### Best-effort internal retries for first statement in a transaction

Note that we see the error message `All transparent retries exhausted` in the preceding example because if the transaction T1, when executing the first statement, finds another concurrent conflicting transaction with equal or higher priority, then T1 will perform a few retries with exponential backoff before giving up in anticipation that the other transaction will be done in some time. The number of retries are configurable by the `ysql_max_write_restart_attempts` YB-TServer flag and the exponential backoff parameters are the same as the ones described in [Performance tuning](../read-committed/#performance-tuning).

Each retry will use a newer snapshot of the database in anticipation that the conflicts might not occur. This is done because if the read time of the new snapshot is higher than the commit time of the earlier conflicting transaction T2, the conflicts with T2 would essentially be voided as T1 and T2 would no longer be "concurrent".

Note that the retries will not be performed in case the amount of data to be sent from YSQL to the client proxy exceeds the TServer flag `ysql_output_buffer_size`.

## Wait-on-Conflict

Wait-on-conflict is {{<badge/ea>}}.

This mode of concurrency control is applicable only for YSQL and provides the same semantics as PostgreSQL.

In this mode, transactions are not assigned priorities. If a conflict occurs when a transaction T1 tries to read, write, or lock a row in a conflicting mode with a few other concurrent transactions, T1 will **wait** until all conflicting transactions finish by either committing or rolling back. Once all conflicting transactions have finished, T1 will:

1. Make progress if the conflicting transactions didn't commit any permanent modifications that conflict with T1.
2. Abort otherwise.

`Wait-on-Conflict` behavior can be enabled by setting the YB-TServer flag `enable_wait_queues=true`, which will enable use of in-memory wait queues that provide waiting semantics when conflicts are detected between transactions. A rolling restart is needed for the flag to take effect. Without this flag set, transactions operate in the priority based `Fail-on-Conflict` mode by default.

Because T1 can make progress only if the conflicting transactions didn't commit any conflicting permanent modifications, there are some intricacies in the behaviour. The list of exhaustive cases possible are detailed below in the Examples section.

{{< note >}}

Semantics of [Read Committed](../read-committed/) isolation make sense only with the Wait-on-Conflict behaviour. Refer to [Interaction with concurrency control](../read-committed/#interaction-with-concurrency-control) for more information.

{{</note >}}

{{< note title="Best-effort internal retries also apply to Wait-on-Conflict policy" >}}

The best-effort internal retries described in Fail-on-Conflict apply to Wait-on-Conflict policy as well. YugabyteDB provides this additional enhancement which is not supported by PostgreSQL.

After a transaction T1 (that was waiting for other transactions) unblocks, it could be the case that some conflicting modifications were committed to the database. In this case, T1 has to abort. However, if its still the first statement that was being executed in T1, best-effort internal retries using a later snapshot of the database will be performed to possibly make progress.

{{</note >}}

### Examples

The following examples describe different use cases detailing the Wait-on-Conflict behavior.

1. Note that the examples require you to set the YB-TServer flag `enable_wait_queues=true`.
1. Also, set the YB-TServer flag `ysql_max_write_restart_attempts=0` to disable internal query layer retries on conflict. This is done to illustrate the `Wait-on-Conflict` concurrency control semantics separately without query layer retries. It is not recommended to disable these retries in production.

A restart is necessary for these flags to take effect.

Start by setting up the table you'll use in all of the examples in this section.

```sql
create table test (k int primary key, v int);
insert into test values (1, 1);
insert into test values (2, 2);
```

#### Conflict between two explicit row-level locks

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
 k | v
---+---
 1 | 1
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
select * from test where k=1 for update;
```

<br>

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

(OR)

```sql
rollback;
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
 k | v
---+---
 1 | 1
(1 row)
```

```sql
commit;
```

   </td>
  </tr>
</tbody>
</table>

#### Explicit row-level lock followed by a conflicting write

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where k=1 for share;
```

<br>

```output
 k | v
---+---
 1 | 1
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
update test set v=1 where k=1;
```

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
commit;
```

(OR)

```sql
rollback;
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
UPDATE 1
```

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
</tbody>
</table>

#### Write followed by a conflicting explicit row-level lock

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=1 where k=1;
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
select * from test where k=1 for share;
```

<br>

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
rollback;
```

(OR)

```sql
commit;
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
 k | v
---+---
 1 | 1
(1 row)
```

```sql
commit;
```

(OR)

```output
ERROR:  All transparent retries exhausted. could not serialize
access due to concurrent update
```

```sql
rollback;
```

   </td>
  </tr>
</tbody>
</table>

#### Write followed by a conflicting write

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=1 where k=1;
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
update test set v=1 where k=1;
```

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
rollback;
```

(OR)

```sql
commit;
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

```sql
commit;
```

(OR)

```output
ERROR:  All transparent retries exhausted. Operation failed.
Try again: Value write after transaction start: { days: 19299
time: 17:07:42.577762 } >= { days: 19299 time: 17:07:40.561842 }:
kConflict
```

```sql
rollback;
```

   </td>
  </tr>
</tbody>
</table>

#### Wait queue jumping is allowed

A transaction can jump the queue even if it does conflict with waiting transactions but doesn't conflict with any active transactions.

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
select * from test where k=1 for share;
```

<br>

```output
 k | v
---+---
 1 | 1
(1 row)
```

   </td>
   <td>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
select * from test where k=1 for update;
```

<br>

```output
(waits for T1 to end...)
```

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>
   </td>
   <td>

```sql
select * from test where k=1 for share;
```

<br>

```output
 k | v
---+---
 1 | 1
(1 row)
```

(Doesn't wait for T2 even though it conflicts with the explicit row-level lock T2 is waiting for)
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
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
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
   </td>
   <td>

```output
 k | v
---+---
 1 | 1
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
commit;
```

   </td>
   <td>
   </td>
  </tr>
</tbody>
</table>

#### Rollback of sub-transaction with conflicting write

Suppose a transaction T1 is blocked on some operation of another transaction T2. If that blocking operation was part of a sub-transaction which is later rolled back, then T1 may proceed:

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
savepoint a;
```

```sql
update test set v=1 where k=1;
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
update test set v=1 where k=1;
```

```output
(waits)
```

   </td>
  </tr>
  <tr>
   <td>

```sql
rollback to savepoint a;
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

```sql
commit;
```

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

### Distributed deadlock detection

In the Wait-on-Conflict mode, transactions can wait for each other and result in a deadlock. Setting the YB-TServer flag `enable_deadlock_detection=true` runs a distributed deadlock detection algorithm in the background to detect and break deadlocks. It is always recommended to keep deadlock detection on when `enable_wait_queues=true`, unless it is absolutely certain that the application or workload behavior makes deadlocks impossible. A rolling restart is required for the change to take effect.

Add `enable_deadlock_detection=true` to the list of TServer flags and restart the cluster.

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
begin transaction isolation level repeatable read;
```

<br>

   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>

```sql
begin transaction isolation level repeatable read;
```

<br>

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=2 where k=1;
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
update test set v=4 where k=2;
```

```output
UPDATE 1
```

   </td>
  </tr>
  <tr>
   <td>

```sql
update test set v=6 where k=2;
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
update test set v=6 where k=1;
```

```output
ERROR:  Internal error: Transaction 00da00cd-87fa-431b-9521-253582fb23fe
was aborted while waiting for locks
```

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

### Metrics

All metrics are per tablet.

#### Histograms

1. `wait_queue_pending_time_waiting` (ms): the amount of time a still-waiting transaction has been in the wait queue
2. `wait_queue_finished_waiting_latency` (ms): the amount of time an unblocked transaction spent in the wait queue
3. `wait_queue_blockers_per_waiter`: the number of blockers a waiter is stuck on in the wait queue

#### Counters

1. `wait_queue_waiters_per_blocker`: the number of waiters stuck on a particular blocker in the wait queue
2. `wait_queue_num_waiters`: the number of waiters stuck on a blocker in the wait queue
3. `wait_queue_num_blockers`: the number of unique blockers tracked in a wait queue

### Limitations

Refer to [#5680](https://github.com/yugabyte/yugabyte-db/issues/5680) for limitations.

## Row-level explicit locking clauses

The `NOWAIT` clause for row-level explicit locking doesn't apply to the `Fail-on-Conflict` mode as there is no waiting. It does apply to the `Wait-on-Conflict` policy but is currently supported only for Read Committed isolation. [#12166](https://github.com/yugabyte/yugabyte-db/issues/12166) will extend support for this in the `Wait-on-Conflict` mode for the other isolation levels.

The `SKIP LOCKED` clause is supported in both concurrency control policies and provides a transaction with the capability to skip locking without any error when a conflict is detected. However, it isn't supported for Serializable isolation. [#11761](https://github.com/yugabyte/yugabyte-db/issues/5683) tracks support for `SKIP LOCKED` in Serializable isolation.
