---
title: Isolation Levels
linkTitle: Isolation Levels
description: Isolation Levels
aliases:
  - /architecture/transactions/isolation-levels/
menu:
  latest:
    identifier: architecture-isolation-levels
    parent: architecture-acid-transactions
    weight: 1151
isTocNested: true
showAsideToc: true
---

YugaByte DB supports two isolation levels.

- [Snapshot Isolation](https://en.wikipedia.org/wiki/Snapshot_isolation), also known as SI, which is an transaction isolation level that guarantees that all reads made in a transaction will see a consistent snapshot of the database, and the transaction itself will successfully commit only if no updates it has made conflict with any concurrent updates made by transactions that committed since that snapshot. 
- [Serializable](https://en.wikipedia.org/wiki/Isolation_(database_systems)#Serializable) isolation level, which would guarantee that transactions run in a way equivalent to a serial (sequential) schedule.

Note that the isolation level support varies between the APIs.

- The [YSQL](../../../api/ysql/) API supports both Serializable and Snapshot Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE` and `REPEATABLE READS` respectively. Note that YSQL Serializable support was added in [v1.2.6](../../../releases/v1.2.6/).
- The [YCQL](../../../api/ycql//dml_transaction/) API supports only Snapshot Isolation using the `BEGIN TRANSACTION` syntax.


## Locks for Isolation Levels

In order to support these two isolation levels, the lock manager internally supports three types
of locks:

### Snapshot Isolation Write Lock 

This type of a lock is taken by a snapshot isolation transaction on values that it modifies.

### Serializable Read Lock 

This type of a lock is taken by serializable read-modify-write transactions on values that they read in order to guarantee they are not modified until the transaciton commits.

### Serializable Write Lock

This type of a lock is taken by serializable transactions on values they write, as well as by pure-write snapshot isolation transactions. Multiple snapshot-isolation transactions writing the same item can thus proceed in parallel.

The following matrix shows conflicts between locks of different types at a high level.

<table>
  <tbody>
    <tr>
      <th></th>
      <th>Snapshot Isolation Write</th>
      <th>Serializable Write</th>
      <th>Serializable Read</th>
    </tr>
    <tr>
      <th>Snapshot Isolation Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Serializable Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Serializable Read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
  </tbody>
</table>


## Fine Grained Locking

We make further distinction between locks acquired on a DocDB node that is being written to by any
transaction or read by a read-modify-write serializable transaction, and locks acquired on its
parent nodes. We call the former types of locks "strong locks" and the latter "weak locks". For
example, if an SI transaction is setting column `col1` to a new value in row `row1`, it will
acquiire a weak SI write lock on `row1` but a strong SI write lock on `row1.col1`. Because of this
distinction, the full conflict matrix actually looks a bit more complex:

<table>
  <tbody>
    <tr>
      <th></th>
      <th>Strong SI Write</th>
      <th>Weak SI Write</th>
      <th>Strong Serializable Write</th>
      <th>Weak Serializable Write</th>
      <th>Strong Serializable Read</th>
      <th>Weak Serializable Read</th>
    </tr>
    <tr>
      <th>Strong SI Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Weak SI Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Strong Serializable Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Weak Serializable Write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Strong Serializable Read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Weak Serializable Read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
  </tbody>
</table>

Here are a couple of examples explaining possible concurrency scenarious from the above matrix:

  - Multiple SI transactions could be modifying different columns in the same row concurrently. They
    acquire weak SI locks on the row key, and  strong SI locks on the individual columns they are
    writing to. The weak SI locks on the row do not conflict with each other.
  - Multiple write-only transactions can write to the same column, and the strong serializable write
    locks that they acquire on this column do not conflict. The final value is determined using the
    hybrid timestamp (the latest hybrid timestamp wins). Note that pure-write SI and serializable
    write operations use the same lock type, because they share the same pattern of conflicts with
    other lock types.
