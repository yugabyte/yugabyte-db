---
title: Transaction isolation levels
headerTitle: Transaction isolation levels
linkTitle: Isolation levels
description: Learn how YugabyteDB supports two transaction isolation levels Snapshot Isolation and Serializable.
menu:
  preview:
    identifier: architecture-isolation-levels
    parent: architecture-acid-transactions
    weight: 20
type: docs
---

Transaction isolation is foundational to handling concurrent transactions in databases. The SQL-92 standard defines four levels of transaction isolation (in decreasing order of strictness): Serializable, Repeatable Read, Read Committed, and Read Uncommitted.

YugabyteDB supports the following three strictest transaction isolation levels:

1. Read Committed {{<badge/tp>}}, which maps to the SQL isolation level of the same name. This isolation level guarantees that each statement sees all data that has been committed before it is issued (this implicitly also means that the statement sees a consistent snapshot). In addition, this isolation level internally handles read restart and conflict errors. In other words, the client does not see read restart and conflict errors (barring an exception).
2. Serializable, which maps to the SQL isolation level of the same name. This isolation level guarantees that transactions run in a way equivalent to a serial (sequential) schedule.
3. Snapshot, which maps to the SQL Repeatable Read isolation level. This isolation level guarantees that all reads made in a transaction see a consistent snapshot of the database, and the transaction itself can successfully commit only if no updates it has made conflict with any concurrent updates made by transactions that committed after that snapshot.

Transaction isolation level support differs between the YSQL and YCQL APIs:

- [YSQL](../../../api/ysql/) supports Serializable, Snapshot, and Read Committed {{<badge/tp>}} isolation levels.
- [YCQL](../../../api/ycql/dml_transaction/) supports only Snapshot isolation using the `BEGIN TRANSACTION` syntax.

Similarly to PostgreSQL, you can specify Read Uncommitted for YSQL, but it behaves the same as Read Committed.

Read Committed is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default, this flag is `false`, in which case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot isolation. The default isolation level for the YSQL API is essentially Snapshot because Read Committed, which is the YSQL API and PostgreSQL syntactic default, maps to Snapshot isolation.

## Internal locking in DocDB

In order to support the three isolation levels, the lock manager internally supports the following three types of locks:

- Serializable read lock is taken by serializable transactions on values that they read in order to guarantee they are not modified until the transaction commits.

- Serializable write lock is taken by serializable transactions on values they write.

- Snapshot isolation write lock is taken by a snapshot isolation (and also read committed) transaction on values that it modifies.

The following matrix shows conflicts between these types of locks at a high level:

<table>
  <tbody>
    <tr>
      <th></th>
      <th>Snapshot isolation write</th>
      <th>Serializable write</th>
      <th>Serializable read</th>
    </tr>
    <tr>
      <th>Snapshot isolation write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Serializable write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Serializable read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
  </tbody>
</table>

That is, serializable read locks block writers but allow other simultaneous readers.  Serializable write locks block readers as expected but not other serializable writers.  Finally, snapshot isolation write locks block all other readers and writers.

Because serializable write locks do not block other serializable writers, concurrent blind writes are allowed at the serializable isolation level.  A blind write is a write to a location that has not been previously read by that transaction.  Two serializable transactions blindly writing to the same location can proceed in parallel assuming there are no other conflicts; the value of the location afterwards will be the value written by the transaction that committed last.

Although described here as a separate lock type for simplicity, the snapshot isolation write lock type is actually implemented internally as a combination of the other two lock types.  That is, taking a single snapshot isolation write lock is equivalent to taking both a serializable read lock and a serializable write lock.

## Locking granularities

Locks can be taken at many levels of granularity.  For example, a serializable read lock could be taken at the level of an entire tablet, a single row, or a single column of a single row.  Such a lock will block attempts to take write locks at that or finer granularities. Thus, for example, a read lock taken at the row level will block attempts to write to that entire row or any column in that row.

In addition to the above-mentioned levels of granularity, locks in DocDB can be taken at  prefixes of the primary key columns, treating the hash columns as a single unit.  For example, if you created a YSQL table via:

```sql
CREATE TABLE test (h1 INT, h2 INT, r1 INT, r2 INT, v INT w INT PRIMARY KEY ((h1,h2) HASH, r1 ASC, r2 ASC);
```

then any of the following objects could be locked:

- the entire tablet
- all rows having h1=2, h2=3
- all rows having h1=2, h2=3, r1=4
- the row having h1=2, h2=3, r1=4, r2=5
- column v of the row having h1=2, h2=3, r1=4, r2=5

With YCQL, granularities exist below the column level; for example, only one key of a column of map data type can be locked.

## Efficiently detecting conflicts between locks of different granularities

The straightforward way to handle locks of different granularities would be to have a map from lockable objects to lock types.  However, this is too inefficient for detecting conflicts: attempting, for example, to add a lock at the tablet level would require checking for locks at every row and column in that tablet.

To make conflict detection efficient, YugabyteDB stores extra information for each lockable object about any locks on sub-objects of it.  In particular, instead of just taking a lock on _X_, it takes a normal lock on _X_ and also weaker versions of that lock on all objects that enclose _X_.  The normal locks are called _strong_ locks and the weaker variants _weak_ locks.

As an example, pretend YugabyteDB has only tablet- and row-level granularities. To take a serializable write lock at the row level (say on row _r_ of tablet _b_), it would take a strong write lock at the row level (on _r_) and a weak write lock at the tablet level (on _b_).  To take a serializable read lock at the tablet level (assume also on _b_), YugabyteDB would just take a strong read lock at the tablet level (on _b_).

Using the following conflict rules, YugabyteDB can decide if two original locks would conflict based only on whether or not their strong/weak locks at any lockable object would conflict:

- two strong locks conflict if and only if they conflict ignoring their strength
  - for example, serializable write conflicts with serializable read per the previous matrix
- two weak locks never conflict
- a strong lock conflicts with a weak lock if and only if they conflict ignoring their strength

That is, for each lockable object that would have two locks, would they conflict under the above rules?  There is no need to enumerate the sub-objects of any object.

Consider our example with a serializable write lock at the row level and a serializable read lock at the tablet level.  A conflict is detected at the tablet level because the strong read and the weak write locks on _b_ conflict because ordinary read and write locks conflict.

What about a case involving two row-level snapshot isolation write locks on different rows in the same tablet?  No conflict is detected because the tablet-level locks are weak and the strong row-level locks are on different rows. If they had involved the same row then a conflict would be detected because two strong snapshot isolation write locks conflict.

Including the strong/weak distinction, the full conflict matrix becomes:

<table>
  <tbody>
    <tr>
      <th></th>
      <th>Strong Snapshot isolation write</th>
      <th>Weak Snapshot isolation write</th>
      <th>Strong Serializable write</th>
      <th>Weak Serializable write</th>
      <th>Strong Serializable read</th>
      <th>Weak Serializable read</th>
    </tr>
    <tr>
      <th>Strong Snapshot isolation write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Weak Snapshot isolation write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Strong Serializable write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
    </tr>
    <tr>
      <th>Weak Serializable write</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Strong Serializable read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
    <tr>
      <th>Weak Serializable read</th>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td class="txn-conflict">&#x2718; Conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
      <td>&#x2714; No conflict</td>
    </tr>
  </tbody>
</table>
