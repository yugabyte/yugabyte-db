---
title: Concurrency control
headerTitle: Concurrency control
linkTitle: Concurrency control
description: Details about Concurrency control in YSQL
menu:
  v2025.1:
    identifier: architecture-concurrency-control
    parent: architecture-acid-transactions
    weight: 600
type: docs
---

[Concurrency control](https://en.wikipedia.org/wiki/Concurrency_control) in databases ensures that multiple transactions can execute concurrently while preserving data integrity. Concurrency control is essential for correctness in environments where two or more transactions can access the same data at the same time.

YugabyteDB provides two policies to handle conflicts between concurrent transactions as described in the following sections.

For information on how row-level explicit locking clauses behave with these concurrency control policies, refer to [Row-level explicit locking clauses](#row-level-explicit-locking-clauses).

## Fail-on-Conflict

This concurrency control strategy is applicable for `Repeatable Read` and `Serializable` isolation levels.

In this mode, transactions are assigned random priorities with some exceptions as described in [Transaction Priorities](../transaction-priorities/). As an exception, all transactions in Read Committed isolation have the same priority set to the highest value (in other words, no transaction can preempt an active Read Committed isolation transaction).

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

Note that we see the error message `All transparent retries exhausted` in the preceding example because if the transaction T1, when executing the first statement, finds another concurrent conflicting transaction with equal or higher priority, then T1 will perform a few retries with exponential backoff before giving up in anticipation that the other transaction will be done in some time. The number of retries are configurable by the `yb_max_query_layer_retries` YSQL configuration parameter and the exponential backoff parameters are the same as the ones described in [Performance tuning](../read-committed/#performance-tuning).

Each retry will use a newer snapshot of the database in anticipation that the conflicts might not occur. This is done because if the read time of the new snapshot is higher than the commit time of the earlier conflicting transaction T2, the conflicts with T2 would essentially be voided as T1 and T2 would no longer be "concurrent".

Note that the retries will not be performed in case the amount of data to be sent from YSQL to the client proxy exceeds the TServer flag `ysql_output_buffer_size`.

## Wait-on-Conflict

This mode of concurrency control is applicable only for YSQL (where it is the default) and provides the same semantics as PostgreSQL.

In this mode, transactions are not assigned priorities. If a conflict occurs when a transaction T1 tries to read, write, or lock a row in a conflicting mode with a few other concurrent transactions, T1 will **wait** until all conflicting transactions finish by either committing or rolling back. Once all conflicting transactions have finished, T1 will:

1. Make progress if the conflicting transactions didn't commit any permanent modifications that conflict with T1.
2. Abort otherwise.

`Wait-on-Conflict` behavior can be enabled by setting the YB-TServer flag `enable_wait_queues=true`, which will enable use of in-memory wait queues that provide waiting semantics when conflicts are detected between transactions. A rolling restart is needed for the flag to take effect. Without this flag set, transactions operate in the priority based `Fail-on-Conflict` mode by default.

Because T1 can make progress only if the conflicting transactions didn't commit any conflicting permanent modifications, there are some intricacies in the behaviour. The list of exhaustive cases possible are detailed below in the Examples section.

{{< note title="Best-effort internal retries also apply to Wait-on-Conflict policy" >}}

The best-effort internal retries described in Fail-on-Conflict apply to Wait-on-Conflict policy as well. YugabyteDB provides this additional enhancement which is not supported by PostgreSQL.

After a transaction T1 (that was waiting for other transactions) unblocks, it could be the case that some conflicting modifications were committed to the database. In this case, T1 has to abort. However, if its still the first statement that was being executed in T1, best-effort internal retries using a later snapshot of the database will be performed to possibly make progress.

{{</note >}}

### Examples

The following examples describe different use cases detailing the Wait-on-Conflict behavior. To run the examples, you need to do the following:

1. Set the YB-TServer flag `enable_wait_queues=true`.
1. Set the per-session `yb_max_query_layer_retries=0` YSQL configuration parameter to disable internal query layer retries on conflict. This is done to illustrate the `Wait-on-Conflict` concurrency control semantics separately without query layer retries. It is not recommended to disable these retries in production. To set it at the cluster level, use the `ysql_pg_conf_csv` YB-TServer flag.

A restart is necessary for the flags to take effect.

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

In the Wait-on-Conflict mode, transactions can wait for each other and result in a deadlock. By default, any cluster with wait queues enabled will be running a distributed deadlock detection algorithm in the background to detect and break deadlocks. It's possible to explicitly disable deadlock detection by setting the YB-TServer flag `disable_deadlock_detection=true`, but this is generally not recommended unless it is absolutely certain that the application or workload behavior makes deadlocks impossible.

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

### Versioning and upgrades

When turning `enable_wait_queues` on or off, or during a rolling restart, where during an update the flag could be on nodes with a more recent version, if some nodes have wait-on-conflict behavior enabled and some don't, you will experience mixed (but still correct) behavior.

A mix of both fail-on-conflict and wait-on-conflict traffic results in the following additional YSQL-specific semantics:

- If a transaction using fail-on-conflict encounters transactions that have conflicting writes -
  - If there is even a single conflicting transaction that uses wait-on-conflict, the transaction aborts.
  - Otherwise, YugabyteDB uses the regular [fail-on-conflict semantics](#fail-on-conflict), which is to abort the lower priority transaction.
- If a transaction using wait-on-conflict encounters transactions that have conflicting writes, it waits for all conflicting transactions to end (including any using fail-on-conflict semantics).

### Fairness

When multiple requests are waiting on the same resource in the wait queue, and that resource becomes available, YugabyteDB generally uses the following process to decide in which order those waiting requests should get access to the contentious resource:

1. Sort all waiting requests based on the _transaction start time_, with requests from the oldest transactions first.
2. Resume requests in order:
    1. Re-run conflict resolution and acquire locks on the requested resource.
    2. If the resource is no longer available because another waiting request acquired it, re-enter the wait queue.

YugabyteDB has two mechanisms to detect that a resource has become available:

1. Direct signal from the transaction coordinator
    - Signals are sent with best effort and may not always arrive immediately or in-order
2. Polling from the wait queue to the transaction coordinator
    - Ensures guaranteed/bounded detection of resource availability

Polling from the wait queue is controlled by the flag `wait_queue_poll_interval_ms`, which is set to 100ms by default. Setting this higher can result in slightly lower overhead, but empirically 100ms seems to offer good performance.

In highly contentious workloads, a low polling interval (around the default 100ms) is required to ensure starvation does not occur. Setting this polling interval higher in contentious settings can cause high tail latency and is not recommended.

### Metrics

All metrics are per tablet.

#### Histograms

1. `wait_queue_pending_time_waiting`: the amount of time in microseconds a still-waiting transaction has been in the wait queue
2. `wait_queue_finished_waiting_latency`: the amount of time in microseconds an unblocked transaction spent in the wait queue
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

## Advisory locks

Advisory locks provide a cooperative, application-managed mechanism for controlling access to custom resources, offering a lighter-weight alternative to row or table locks. In YugabyteDB, all acquired advisory locks are globally visible across all nodes and sessions via the dedicated `pg_advisory_locks` system table, ensuring distributed coordination.

You configure advisory locks using the [Advisory lock flags](../../../reference/configuration/yb-tserver/#advisory-lock-flags).

### Using advisory locks

Advisory locks in YugabyteDB are semantically identical to PostgreSQL, and are managed using the same functions. Refer to [Advisory lock functions](https://www.postgresql.org/docs/15/functions-admin.html#FUNCTIONS-ADVISORY-LOCKS) in the PostgreSQL documentation.

You can acquire an advisory lock in the following ways:

- Session level

    Once acquired at session level, the advisory lock is held until it is explicitly released or the session ends.

    Unlike standard lock requests, session-level advisory lock requests do not honor transaction semantics: a lock acquired during a transaction that is later rolled back will still be held following the rollback, and likewise an unlock is effective even if the calling transaction fails later. A lock can be acquired multiple times by its owning process; for each completed lock request there must be a corresponding unlock request before the lock is actually released.

    ```sql
    SELECT pg_advisory_lock(10);
    ```

- Transaction level

    Transaction-level lock requests, on the other hand, behave more like regular row-level lock requests: they are automatically released at the end of the transaction, and there is no explicit unlock operation. This behavior is often more convenient than the session-level behavior for short-term usage of an advisory lock.

    ```sql
    SELECT pg_advisory_xact_lock(10);
    ```

Advisory locks can also be exclusive or shared:

- Exclusive Lock

    Only one session/transaction can hold the lock at a time. Other sessions/transactions can't acquire the lock until the lock is released.

    ```sql
    select pg_advisory_lock(10);
    select pg_advisory_xact_lock(10);
    ```

- Shared Lock

    Multiple sessions/transactions can hold the lock simultaneously. However, no session/transaction can acquire an exclusive lock while shared locks are held.

    ```sql
    select pg_advisory_lock_shared(10);
    select pg_advisory_xact_lock_shared(10);
    ```

Finally, advisory locks can be blocking or non-blocking:

- Blocking lock

    The process trying to acquire the lock waits until the lock is acquired.

    ```sql
    select pg_advisory_lock(10);
    select pg_advisory_xact_lock(10);
    ```

- Non-blocking lock

    The process immediately returns a boolean value stating if the lock is acquired or not.

    ```sql
    select pg_try_advisory_lock(10);
    select pg_try_advisory_xact_lock(10);
    ```

## Table-level locks

{{<tags/feature/tp idea="1114">}} Table-level locks for YSQL (available in {{<release "2025.1.1.0">}} and later) provide a mechanism to coordinate concurrent DML and DDL operations. The feature provides serializable semantics between DMLs and DDLs by introducing distributed locks on YSQL objects. PostgreSQL clients acquire locks to prevent DMLs and DDLs from running concurrently.

Support for table-level locks is disabled by default, and to enable the feature, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag [enable_object_locking_for_table_locks](../../../explore/transactions/explicit-locking/#enable-table-level-locks) to true.

Table-level locks in YugabyteDB are semantically identical to PostgreSQL, and are managed using the same modes and API. Refer to [Table-level locks](https://www.postgresql.org/docs/15/explicit-locking.html#LOCKING-TABLES) in the PostgreSQL documentation.

PostgreSQL table locks can be broadly categorized into two types:

**Shared locks:**

- `ACCESS SHARE`
- `ROW SHARE`
- `ROW EXCLUSIVE`

**Global locks:**

- `SHARE UPDATE EXCLUSIVE`
- `SHARE`
- `SHARE ROW EXCLUSIVE`
- `EXCLUSIVE`
- `ACCESS EXCLUSIVE`

DMLs acquire shared locks alone, while DDLs acquire a combination of shared and global locks.

To reduce the overhead of DMLs, YugabyteDB takes shared locks on the PostgreSQL backend's host TServer alone. Global locks are propagated to the Master leader.

Each TServer maintains an in-memory `TSLocalLockManager` that serves object lock acquire or release calls. The Master also maintains an in-memory lock manager primarily for serializing conflicting global object locks. When the Master leader receives a global lock request, it first acquires the lock locally, then fans out the lock request to all TServers with valid [YSQL leases](#ysql-lease-mechanism), and executes the client callback after the lock has been successfully acquired on all TServers (with a valid YSQL lease).

The following illustration describes how a DDL progresses in YugabyteDB:

![Table-level locks](/images/architecture/txn/table-level-locks.png)

All statements inherently acquire some object locks. You can also explicitly acquire object locks using the [LOCK TABLE API](https://www.postgresql.org/docs/current/sql-lock.html):

```sql
-- Acquire a share lock on a table
LOCK TABLE my_table IN SHARE MODE;

-- Acquire an access exclusive lock (blocks all other operations)
LOCK TABLE my_table IN ACCESS EXCLUSIVE MODE;
```

You can observe active object locks using the [pg_locks](../../../explore/observability/pg-locks/) system view for transactions older than [yb_locks_min_txn_age](../../../explore/observability/pg-locks/#yb-locks-min-txn-age):

```sql
SELECT * FROM pg_locks WHERE NOT granted;
```

### Lock scope and lifecycle

All object locks are tied to a DocDB transaction:

- **DMLs**: Locks are released at commit or abort time.
- **DDLs**: Lock release is delegated to the Master, which has a background task for observing DDL commits or aborts and finalizing schema changes.

When a DDL finishes, all locks corresponding to the transaction are released and this release path enforces cache refresh on the TServers ensuring that they have the latest catalog cache. This also ensures any new DMLs waiting on the same locks to see the latest schema after acquiring the object locks.

To reduce overhead for read-only workloads, YugabyteDB reuses DocDB transactions wherever possible.

### Failure handling

Locks are cleaned up for various failure scenarios as follows:

- PostgreSQL backend crash: The TServer-PostgreSQL session heartbeat cleans up corresponding locks.
- TServer crash: YSQL Lease mechanism cleans up DDL locks that originated from the TServer.
- Master leader crash: Newly elected Master leader replays catalog entries to recreate locks' state held (at the master) and establish the lease for the TServers.

### Important considerations

- If a TServer doesn't have a valid YSQL lease, it cannot serve any requests from PostgreSQL backends.
- Upon TServer lease changes (due to network issues), all active PostgreSQL backends are killed.
- If TServers cannot communicate with the Master leader for shorter durations (less than the YSQL lease), DDLs will stall because global locks cannot be served.
- If the length of time during which TServers cannot communicate with the Master exceeds the YSQL lease timeout, they lose their YSQL lease and all PostgreSQL backends are killed (until the connection with the Master is reestablished).

## YSQL lease mechanism

YugabyteDB employs a robust lease mechanism between YB-TServer and YB-Master, so that in the event a YB-TServer is network partitioned from the YB-Master leader for a long time, the YB-TServer is transitioned to a mode that doesn't allow the YB-TServer to serve read or write traffic till it establishes connectivity with the YB-Master leader.

### Configure leases

You enable the lease feature for the YB-TServers using the [--enable_ysql_operation_lease](../../../reference/configuration/yb-tserver/#enable-ysql-operation-lease) flag.

You configure the lease duration and behavior using the following additional flags:

| Server | Flag |
| :--- | :--- |
| Master | [--master_ysql_operation_lease_ttl_ms](../../../reference/configuration/yb-master/#master-ysql-operation-lease-ttl-ms) |
| Master | [--ysql_operation_lease_ttl_client_buffer_ms](../../../reference/configuration/yb-master/#ysql-operation-lease-ttl-client-buffer-ms) |
| TServer | [--ysql_lease_refresher_interval_ms](../../../reference/configuration/yb-tserver/#ysql-lease-refresher-interval-ms) |

#### master_ysql_operation_lease_ttl_ms

Default: `5 * 60 * 1000` (5 minutes)

Specifies base YSQL lease Time-To-Live (TTL). The YB-Master leader uses this value to determine the validity of a YB-TServer's YSQL lease.

This parameter primarily determines the TTL of YSQL lease extensions. The YB-Master leader considers a YB-TServer's YSQL lease valid for this specified number of milliseconds after it successfully processes the YB-TServer's most recent `RefreshYsqlLease` request. YB-TServers will terminate all hosted PostgreSQL sessions if they are unable to refresh their YSQL lease before its expiration.

Increasing the lease TTL has the following implications:

- Decreases the chances YB-TServers will terminate all hosted PostgreSQL sessions because of YSQL lease expiration.

- Extends the period during which DDLs remain serviceable after a YB-TServer becomes unavailable (this period is capped by the lease TTL).

- Increases the time before locks held by crashed YB-TServers are released, which can block DMLs on any table where the  crashed YB-TServer held a lock (this period  is also capped by the lease TTL).

#### ysql_operation_lease_ttl_client_buffer_ms

Default: 2000 (2 seconds)

Specifies a client-side buffer for the YSQL operation lease TTL.

When processing lease refresh RPC (`RefreshYsqlLease`) requests, the YB-Master leader subtracts this value from the [--master_ysql_operation_lease_ttl_ms](#master-ysql-operation-lease-ttl-ms) to calculate a slightly shorter lease TTL that is then provided to the YB-TServers. This difference between the lease TTL maintained by the YB-Masters and the lease TTL granted to the YB-TServers allows YB-TServers a grace period to terminate their hosted sessions before the YB-Master leader considers their lease expired.

#### ysql_lease_refresher_interval_ms

Default: `1000` (1 second)

Determines the interval a YB-TServer waits before initiating another YSQL lease refresh RPC. After a YB-TServer processes either a successful response or an error status from a YSQL lease refresh RPC, it pauses for this specified duration before sending another YSQL lease refresh RPC.

With the YSQL lease feature enabled, a YB-TServer node will only accept PostgreSQL connections after establishing a lease with the YB-Master leader. If a YB-TServer is unable to refresh its lease before it expires, it will terminate all PostgreSQL sessions it hosts.
When a YB-TServer loses its lease, clients connected to that YB-TServer will encounter the following specific error messages:

```output
server closed the connection unexpectedly
```

```output
Object Lock Manager Shutdown
```

### Lease persistence and YB-Master failover

YSQL leases held by YB-TServer are respected by a new YB-Master leader after a failover. When a YB-TServer first acquires a YSQL lease, the YB-Master persists this to the system catalog. If a YB-TServer's lease expires, the entry is removed. After a YB-Master leader failover, the new YB-Master leader treats every YB-TServer with an active YSQL lease entry in its persisted state as having a live lease, with the remaining TTL being the full YSQL lease duration. This design ensures that YB-TServers do not unnecessarily terminate connections during a YB-Master failover, contributing to higher availability.

<!-- ### Changing the lease TTL

The lease TTL, in particular with the `master_ysql_operation_lease_ttl_ms` configuration flag, can be safely increased during runtime. However lowering it during runtime may be unsafe as the lease duration from the YB-Master leader's perspective may be shorter than the lease duration from the YB-TServer's perspective. If the YB-TServer is network partitioned from the YB-Master leader during this time, it may serve DMLs believing it has a live lease while the YB-Master leader serves DDLs believing the YB-TServer has lost its lease.

To prevent such inconsistencies when reducing `master_ysql_operation_lease_ttl_ms`, use the following multi-step process.

Suppose `X` is the target value for `master_ysql_operation_lease_ttl_ms`; do the following:

1. Set `ysql_operation_lease_ttl_client_buffer_ms` on all YB-Masters as follows:

    (`master_ysql_operation_lease_ttl_ms` - `X` + `ysql_operation_lease_ttl_client_buffer_ms`)
1. Wait for `master_ysql_operation_lease_ttl_ms`.
1. Set `master_ysql_operation_lease_ttl_ms` to `X`.
1. Set `ysql_operation_lease_ttl_client_buffer_ms` to its original value before the change in step 1.

For example, suppose `master_ysql_operation_lease_ttl_ms` is 30 seconds (30000 ms) and you want to reduce it to 20 seconds (20000 ms). Your `ysql_operation_lease_ttl_client_buffer_ms` is 2 seconds (2000 ms). The steps would be:

1. Set `ysql_operation_lease_ttl_client_buffer_ms` to 12 seconds. -->
