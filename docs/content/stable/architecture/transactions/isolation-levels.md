---
title: Transaction isolation levels
headerTitle: Transaction isolation levels
linkTitle: Transaction isolation levels
description: Learn how YugabyteDB supports two transaction isolation levels Snapshot Isolation and Serializable.
menu:
  stable:
    identifier: architecture-isolation-levels
    parent: architecture-acid-transactions
    weight: 1152
type: docs
---

Transaction isolation is foundational to handling concurrent transactions in databases. The SQL-92 standard defines four levels of transaction isolation (in decreasing order of strictness): Serializable, Repeatable Read, Read Committed, and Read Uncommitted.

YugabyteDB supports the following three strictest transaction isolation levels:

1. Read Committed<sup>$</sup>, which maps to the SQL isolation level of the same name. This isolation level guarantees that each statement sees all data that has been committed before it is issued (this implicitly also means that the statement sees a consistent snapshot). In addition, this isolation level internally handles read restart and conflict errors. In other words, the client does not see read restart and conflict errors (barring an exception).
2. Serializable, which maps to the SQL isolation level of the same name. This isolation level guarantees that transactions run in a way equivalent to a serial (sequential) schedule.
3. Snapshot, which maps to the SQL Repeatable Read isolation level. This isolation level guarantees that all reads made in a transaction see a consistent snapshot of the database, and the transaction itself can successfully commit only if no updates it has made conflict with any concurrent updates made by transactions that committed after that snapshot.

Transaction isolation level support differs between the YSQL and YCQL APIs:

- [YSQL](../../../api/ysql/) supports Serializable, Snapshot, and Read Committed<sup>$</sup> isolation levels.
- [YCQL](../../../api/ycql/dml_transaction/) supports only Snapshot isolation using the `BEGIN TRANSACTION` syntax.

<sup>$</sup> Read Committed support is currently in [Beta](/preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag). This level is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default, this flag is `false`, in which case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot isolation (together with YSQL Read Committed and Read Uncommitted also in turn using the Snapshot isolation). The default isolation level for the YSQL API is essentially Snapshot because Read Committed, which is the YSQL API and PostgreSQL syntactic default, maps to Snapshot isolation.

## Internal locking in DocDB

In order to support the three isolation levels, the lock manager internally supports the following three types of locks:

- Snapshot isolation write lock is taken by a snapshot (and also read committed) isolation transaction on values that it modifies.

- Serializable read lock is taken by serializable read-modify-write transactions on values that they read in order to guarantee they are not modified until the transaction commits.

- Serializable write lock is taken by serializable transactions on values they write, as well as by pure-write snapshot isolation transactions. Multiple snapshot-isolation transactions writing the same item can thus proceed in parallel.

The following matrix shows conflicts between locks of different types at a high level:

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


## Fine-grained locking

Further distinction exists between locks acquired on a DocDB node that is being written to by any
transaction or read by a read-modify-write serializable transaction, and locks acquired on its parent nodes. The former types of locks are referred to as strong locks and the latter as weak locks. For example, if a snapshot isolation transaction is setting column `col1` to a new value in row `row1`, it acquires a weak snapshot isolation write lock on `row1` but a strong snapshot isolation write lock on `row1.col1`. Because of this distinction, the full conflict matrix actually looks more complex, as follows:

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


Here are some examples explaining possible concurrency scenarios from the preceding matrix:

- Multiple snapshot isolation transactions could be modifying different columns in the same row concurrently. They acquire weak snapshot isolation locks on the row key, and strong snapshot isolation locks on the individual columns to which they are writing. The weak snapshot isolation locks on the row do not conflict with each other.
- Multiple write-only transactions can write to the same column, and the strong serializable write locks that they acquire on this column do not conflict. The final value is determined using the hybrid timestamp (the latest hybrid timestamp wins). Note that pure-write snapshot isolation and serializable write operations use the same lock type, because they share the same pattern of conflicts with other lock types.
