## Wait-on-Conflict Concurrency Control in YSQL


## 1 Introduction

This document aims to define fail-on-conflict concurrency control behavior in YSQL, wait-on-conflict behavior in PostgreSQL, and outline the requirements for changing YSQL's default concurrency-control semantics.

To understand wait-on-conflict concurrency control in PostgreSQL, it is important to take note of some points about row-level locking and its interaction with DMLs.

### PostgreSQL supports 4 types of row-level locks -

1. Exclusive modes (i.e., only 1 transaction can hold such a lock at any time) -
    1. `FOR UPDATE` - takes exclusive lock on the whole row (blocks the shared locks as well)
    2. `FOR NO KEY UPDATE` - takes exclusive lock on row but allows a shared lock on primary key i.e., `FOR KEY SHARE`
2. Share access modes
    1. `FOR SHARE` - shared lock on full row i.e., blocks all exclusive locks
    2. `FOR KEY SHARE` - shared lock on key i.e., allows `FOR NO KEY UPDATE`

### PostgreSQL has two notions of conflict -

1. **Lock-modification conflict:** (modification is a change in tuple via a DML) _(see example 2)._
    1. `FOR UPDATE`: doesn’t allow any concurrent modification to the locked tuple.
    2. `FOR NO KEY UPDATE`: doesn’t allow any concurrent modification to the locked tuple.
    3. `FOR SHARE`: doesn’t allow any concurrent modification to the locked tuple.
    4. `FOR KEY SHARE`: allow only modification to non-primary key cols

Lock-modification conflict only happens when the period from lock acquire to lock release intersects/overlaps with the read to commit time of the transaction performing the modification.

Lock-modification conflicts result in serialization errors. Note that the lock might be issued before or after the modification (see example 2i and 2ii).

Note that in READ COMMITTED isolation, when a lock-modification conflict occurs, PostgreSQL does some post-processing (called “READ COMMITTED update checking steps”) instead of throwing a 40001.

2. **Lock-lock conflict:** this conflict only refers to one lock blocking another i.e., just waiting behaviour. It **doesn’t** refer to a serialization error. In other words, two conflicting locks can only result in blocking and not in a serialization error (or “write” conflict as we roughly call it). Also note that these locks can be taken explicitly using a `SELECT FOR `or implicitly using a DML. So there can be 4 cases - implicit-implicit, implicit-explicit, explicit-implicit, explicit-explicit lock-lock conflicts.

    _(see example 1 for explicit lock-explicit lock conflict)._

DMLs implicitly take the following row-level locks:

1. `DELETE` -> `FOR UPDATE`
2. `UPDATE` -> `FOR UPDATE` if updating a column which has a unique index on it (one that can be used in a foreign key)
3. All `UPDATEs` other than those in point 2 -> `FOR NO KEY UPDATE`.

In other words, a DML operation consists of _locking_ and _modification_ of a tuple.

Given that writing = locking + modification, the definition of lock-modification conflict is more generic to think of for serialization errors (the usual write-write “conflict” we talk about is just a special case of this. A write-write conflict = implicit lock - modification conflict).

### Fail-on-Conflict in YSQL -

YSQL does not differentiate between a lock-lock conflict and a lock-modification conflict. In either case, such conflicts will be detected during conflict resolution. Conflict resolution in YSQL would not lead to any blocking, but would surely abort either itself or all conflicting transactions based on priorities of the transactions (which are randomly chosen). We call this behavior _fail-on-conflict concurrency control_.

### Wait-on-Conflict in PostgreSQL -

PostgreSQL uses **wait-on-conflict concurrency control**, where a transaction will **wait upon encountering a lock-lock conflict** in order to acquire the locks it needs and proceed.

**Wait-on-conflict (In PostgreSQL):** When a transaction T1 attempts to take a lock (either implicitly via a write or explicitly using `SELECT FOR`), it might be blocked by transactions that hold a conflicting lock (note again, either implicitly via a write or explicitly using `SELECT FOR`). The transaction will then wait for all lock-lock conflicting transactions to end before obtaining the lock. Once the other transactions end, either the lock will be taken or a **lock-modification** conflict will be detected and a serialization error will occur.

Some points to note are -

1. **Transaction might skip being added to the queue:** If a transaction is trying to acquire a lock that doesn’t conflict with actively held locks but does conflict with a transaction that is already waiting to acquire a conflicting lock, it proceeds to take the lock. In short, the transaction skips being added to the queue itself (see example 3). This improves availability at the potential cost of starvation.
2. **Transaction already in queue might jump older transactions in the queue:** Multiple transactions might be blocked to lock the same tuple and this results in a queue. When an active transaction ends, we check other transactions in the wait queue which the resolved transaction was blocking. Transactions which no longer conflict with other active transactions are unblocked to acquire locks and become active. Note that this implies that a transaction that conflicts with earlier transactions blocked in the queue might still be unblocked if it doesn’t conflict with active transactions. This again improves availability at the potential cost of starvation, and matches the behavior in PostgreSQL.
3. In case <code>[statement_timeout or lock_timeout](https://www.postgresql.org/docs/13/runtime-config-client.html)</code> is non-zero, a blocked transaction will abort after the timeout. (example 4). A zero timeout would imply indefinite waiting.

NOTE: Refer this [article](https://postgrespro.com/blog/pgsql/5968005) for a good overview of row-level locking.


## 2 Requirements

1. Firstly, support lock-lock type of conflict that only blocks, in YSQL. This is to split the current notion of conflict in YSQL into the two finer notions that Postgres supports.
2. In case a transaction T1 issues an RPC which tries to acquire a lock as part of a DML that conflicts with existing locks held by active transactions, the RPC should:
    1. wait for all transactions that have conflicting locks to end (waiting behaviour)
    2. throw a serialization error only if a modification that conflicts with the lock has committed
    3. respect client-specified timeouts and remove itself from the queue
3. Implement a [distributed deadlock detection algorithm](https://docs.google.com/document/d/1E4LHGmVZuTlr36_uczPuE6aAjoLT4sfr1o0YCtzUUPc/edit#) to break cycles. The following properties are required -
    1. No false positives
    2. Bound on latency = O (cycle size)
4. Add a metric to measure the “intensity” of starvation by measuring the number instances where a transaction jumps the wait queue ahead of other waiting transactions that it conflicts with just because it doesn’t conflict with active transactions.

Once wait-on-conflict concurrency control is supported, YSQL will have the same behaviour as Postgres with regards to transaction waiting behaviour in case of writing/locking, with **two exceptions**:

1. For Serializable isolation level, there will be an extra YSQL-only behaviour of waiting in case a read (or write) intent is to be written that conflicts with an already existing write (or read) intent from another transaction. In PostgreSQL’s Serializable isolation implementation (i.e., SSI), reads don’t take implicit locks and hence don’t “lock” conflict with other writes. And hence, a read doesn’t wait on another write (and vice versa) in PostgreSQL’s Serializable isolation.
2. YSQL writes fine grained column level intents in case of modifications to specific columns only. This will allow us to be more fine grained so that modifications to different columns of a row need not result in waiting (possibly followed by a serialization error). This is one aspect in which YSQL would turn out to be **better than** PostgreSQL - and semantically different in a hopefully beneficial way.

## 3 Usage

**enable_wait_queues**: a cluster-level gflag to turn on wait-on-conflict behavior. This will require a cluster restart. Note that all transactions in the cluster will either use wait-on-conflict OR fail-on-conflict behavior, and mixed behavior is tolerable during restart/migration but otherwise generally not supported.

### Expected Behavior

```sql
create table test (k int primary key, v int);
insert into test values (1, 1);
```

1. **Explicit lock - Explicit lock interaction (only blocks)**

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> select * from test where k=1 for update;</code>
<p>
<code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> select * from test where k=1 for update;</code>
<p>
<code>&lt;waiting...></code>
   </td>
  </tr>
  <tr>
   <td><code> commit; / rollback;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
  </tr>
</table>




2. **i) Lock followed by write (with explicit - implicit lock conflict), \
 \
ii) write followed by lock (implicit - explicit lock conflict followed by possible lock - modification serialization error if write commits) and \
 \
iii) write followed by write (explicit - explicit lock conflict followed by possible lock - modification serialization error if first write commits)**

For each case, we will only explore the interaction of `FOR SHARE` along with an update \
to a non-primary key column. Recall that `FOR SHARE` doesn’t allow any concurrent modification to the locked tuple.



1. **Lock followed by write**

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> select * from test where k=1 for share;</code>
<p>
<code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>&lt;waiting...></code>
   </td>
  </tr>
  <tr>
   <td><code>commit/ rollback;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>UPDATE 1</code>
<p>
<code>// Takes FOR NO KEY UPDATE lock</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> commit;</code>
   </td>
  </tr>
</table>




2. **Write followed by lock** (i.e., results in implicit lock - explicit lock waiting followed by serialization error if write commits)

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>UPDATE 1</code>
<p>
<code>// Takes FOR NO KEY UPDATE lock</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> select * from test where k=1 for share;</code>
<p>
<code>&lt;waiting...></code>
   </td>
  </tr>
  <tr>
   <td><code>i) rollback;</code>
<p>
<code>ii) commit;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>i)  k | v</code>
<p>
<code>  ---+---</code>
<p>
<code>   1 | 1</code>
<p>
<code>  (1 row)</code>
<p>
<code>ii) ERROR:  could not serialize access due to concurrent update</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>i) > commit;</code>
<p>
<code>ii) > rollback;</code>
   </td>
  </tr>
</table>




3. **Write followed by write**

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>UPDATE 1</code>
<p>
<code>// Takes FOR NO KEY UPDATE lock</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>&lt;waiting for NO KEY UPDATE lock...></code>
   </td>
  </tr>
  <tr>
   <td><code>i) rollback;</code>
<p>
<code>ii) commit;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>i) UPDATE 1</code>
<p>
<code>ii) ERROR:  could not serialize access due to concurrent update</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>i) > commit;</code>
<p>
<code>ii) > rollback;</code>
   </td>
  </tr>
</table>




3. Proof that a transaction T3 won’t block on another waiting transaction that T3 conflicts with, if T3 doesn’t conflict with an active transaction.

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
   <td><code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> select * from test where k=1 for share;</code>
<p>
<code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
   <td>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> select * from test where k=1 for update;</code>
<p>
<code>&lt;waiting for T1 to end...></code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td>
   </td>
   <td><code> select * from test where k=1 for share;</code>
<p>
<code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
<p>
<code>// Doesn't wait for T2 even though it conflicts with it</code>
   </td>
  </tr>
  <tr>
   <td><code>commit;</code>
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
   <td><code>commit;</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> k | v</code>
<p>
<code>---+---</code>
<p>
<code> 1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>commit;</code>
   </td>
   <td>
   </td>
  </tr>
</table>




4. Transaction aborts in case statement_timeout or lock_timeout is hit. Note that lock_timeout applies to both implicit and explicit locks; skipping examples for lock_timeout since it is exactly the same behaviour as for statement_timeout.

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>UPDATE 1</code>
<p>
<code>// Takes FOR NO KEY UPDATE lock</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>set statement_timeout=5000; -- in ms</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>&lt;waiting for NO KEY UPDATE lock...></code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>ERROR:  canceling statement due to statement timeout</code>
<p>
<code>CONTEXT:  while updating tuple (0,5) in relation "test"</code>
   </td>
  </tr>
  <tr>
   <td><code>commit;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>rollback;</code>
   </td>
  </tr>
</table>

## 4 Compatibility

### Cross feature interaction

1. **Interaction with savepoints.** Rolling back a savepoint might result in release of a lock and hence resolve some lock-lock waits.

<table>
  <tr>
   <td>
<code> begin transaction isolation level repeatable read; \
START TRANSACTION</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> begin transaction isolation level repeatable read;</code>
<p>
<code>START TRANSACTION</code>
   </td>
  </tr>
  <tr>
   <td><code> savepoint a;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td><code> update test set v=2 where k=1;</code>
<p>
<code>UPDATE 1</code>
<p>
<code>// Takes FOR NO KEY UPDATE lock</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> select * from test where k=1 for share;</code>
<p>
<code>&lt;waiting...></code>
   </td>
  </tr>
  <tr>
   <td><code> rollback to a;</code>
   </td>
   <td>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code>  k | v</code>
<p>
<code> ---+---</code>
<p>
<code>  1 | 1</code>
<p>
<code>(1 row)</code>
   </td>
  </tr>
  <tr>
   <td>
   </td>
   <td><code> commit;</code>
   </td>
  </tr>
</table>

2. **READ COMMITTED isolation level** (exactly as in PostgreSQL) has a dependency on wait-on-conflict concurrency control. To be precise, on facing a conflict, a transaction has to wait for the conflicting transaction to rollback/commit before retrying the statement. Once unblocked, the read committed session will operate on the newly-committed data when retrying the conflicting statement. If READ COMMITTED is used without wait-on-conflict, we will use an internal retry mechanism which differs slightly from PostgreSQL.

### Versioning and upgrades

This feature is upgrade and downgrade safe. When turning the gflag on/off, or during rolling restarts across versions with the flag “on” in the higher version, if some nodes have wait-on-conflict behavior enabled and some don’t, users will experience mixed (but still correct) behavior. A mix of both fail-on-conflict and wait-on-conflict traffic will result in the following additional YSQL specific semantics -

1. If a transaction using fail-on-conflict sees transactions that have written conflicting intents -
    1. Behaviour is as today
2. If a transaction uses wait-on-conflict and sees transactions that have written conflicting intents -
    1. Wait for all conflicting transactions to end (including any using fail-on-conflict semantics)
