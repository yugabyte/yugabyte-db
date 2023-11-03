---
title: Transactions overview
headerTitle: Transactions overview
linkTitle: Overview
description: An overview of transactions work in YugabyteDB.
menu:
  preview:
    identifier: architecture-transactions-overview
    parent: architecture-acid-transactions
    weight: 1151
type: docs
---

Transactions and strong consistency are a fundamental requirement for any RDBMS. DocDB has been designed for strong consistency. It supports fully distributed atomicity, consistency, isolation, durability (ACID) transactions across rows, multiple tablets, and multiple nodes at any scale. Transactions can span across tables in DocDB.

A transaction is a sequence of operations performed as a single logical unit of work. The intermediate states of the database as a result of applying the operations inside a transaction are not visible to other concurrent transactions, and if a failure occurs that prevents the transaction from completing, then none of the steps affect the database.

Note that all update operations inside DocDB are considered to be transactions, including operations that update only one row, as well as those that update multiple rows that reside on different nodes. If `autocommit` mode is enabled, each statement is executed as one transaction.

## Time synchronization

A transaction in a YugabyteDB cluster may need to update multiple rows that span across nodes in a cluster. In order to be ACID-compliant, the various updates made by this transaction should be visible instantaneously as of a fixed time, irrespective of the node in the cluster that reads the update. To achieve this, the nodes of the cluster must agree on a global notion of time, which requires all nodes to have access to a highly-available and globally-synchronized clock. [TrueTime](https://cloud.google.com/spanner/docs/true-time-external-consistency), used by Google Cloud Spanner, is an example of such a clock with tight error bounds. However, this type of clock is not available in many deployments. Physical time clocks (or wall clocks) cannot be perfectly synchronized across nodes and cannot order events with the purpose to establish a causal relationship across nodes.

### Hybrid logical clocks

YugabyteDB uses hybrid logical clocks (HLC) which solve the problem by combining physical time clocks that are coarsely synchronized using NTP with Lamport clocks that track causal relationships.

Each node in a YugabyteDB cluster first computes its HLC represented as a tuple (physical time component, logical component). HLCs generated on any node are strictly monotonic, and are compared as a tuple. When comparing two HLCs, the physical time component takes precedence over the logical component.

* Physical time component: YugabyteDB uses the physical clock (`CLOCK_REALTIME` in Linux) of a node to initialize the physical time component of its HLC. Once initialized, the physical time component can only be updated to a higher value.

* Logical component: For a given physical time component, the logical component of the HLC is a monotonically increasing number that provides ordering of events happening in that same physical time. This is initially set to 0. If the physical time component is updated at any point, the logical component is reset to 0.

On any RPC communication between two nodes, HLC values are exchanged. The node with the lower HLC updates its HLC to the higher value. If the physical time on a node exceeds the physical time component of its HLC, the latter is updated to the physical time and the logical component is set to 0. Thus, HLCs on a node are monotonically increasing.

The same HLC is used to determine the read point in order to determine which updates should be visible to end clients. If an update has safely been replicated onto a majority of nodes, as per the Raft protocol, that update operation can be acknowledged as successful to the client and it is safe to serve all reads up to that HLC. This forms the foundation for [lockless multiversion concurrency control in YugabyteDB](#multi-version-concurrency-control).

## Multi-version concurrency control

YugabyteDB maintains data consistency internally using multi-version concurrency control (MVCC) without the need to lock rows. Each transaction works on a version of the data in the database as of some hybrid timestamp. This prevents transactions from reading the intermediate updates made by concurrently-running transactions, some of which may be updating the same rows. Each transaction, however, can see its own updates, thereby providing transaction isolation for each database session. Using MVCC minimizes lock contention during the execution of multiple concurrent transactions.

### MVCC using hybrid time

YugabyteDB implements MVCC and internally keeps track of multiple versions of values corresponding to the same key (for example, of a particular column in a particular row), as described in [Persistence on top of RocksDB](../../docdb/persistence). The last part of each key is a timestamp, which enables quick navigation to a particular version of a key in the RocksDB key-value store.

The timestamp used for MVCC comes from the [hybrid time](http://users.ece.utexas.edu/~garg/pdslab/david/hybrid-time-tech-report-01.pdf) algorithm, a distributed timestamp assignment algorithm that combines the advantages of local real-time (physical) clocks and Lamport clocks. The hybrid time algorithm ensures that events connected by a causal chain of the form "A happens before B on the same server" or "A happens on one server, which then sends an RPC to another server, where B happens", always get assigned hybrid timestamps in an increasing order. This is achieved by propagating a hybrid timestamp with most RPC requests, and always updating the hybrid time on the receiving server to the highest value observed, including the current physical time on the server. Multiple aspects of YugabyteDB's transaction model rely on these properties of hybrid time. Consider the following examples:

* Hybrid timestamps assigned to committed Raft log entries in the same tablet always keep increasing, even if there are leader changes. This is because the new leader always has all committed entries from previous leaders, and it makes sure to update its hybrid clock with the timestamp of the last committed entry before appending new entries. This property simplifies the logic of selecting a safe hybrid time to select for single-tablet read requests.

* A request trying to read data from a tablet at a particular hybrid time needs to ensure that no
  changes happen in the tablet with timestamp values lower than the read timestamp, which could lead to an inconsistent result set. The need to read from a tablet at a particular timestamp arises during transactional reads across multiple tablets. This condition becomes easier to satisfy due to the fact that the read timestamp is chosen as the current hybrid time on the YB-TServer processing the read request, so hybrid time on the leader of the tablet being read from immediately becomes updated to a value that is at least as high as the read timestamp. Then the read request only has to wait for any relevant entries in the Raft queue with timestamp values lower than the read timestamp to be replicated and applied to RocksDB, and it can proceed with processing the read request after that.

### Supported isolation levels

YugabyteDB supports the following transaction isolation levels:

* Read Committed, which maps to the SQL isolation level of the same name.
* Serializable, which maps to the SQL isolation level of the same name.
* Snapshot, which maps to the SQL isolation level `REPEATABLE READ`.

For more information, see [isolation levels in YugabyteDB](../isolation-levels).

### Explicit locking

As with PostgreSQL, YugabyteDB provides various row-level lock modes to control concurrent access to data in tables. These modes can be used for application-controlled locking in cases where MVCC does not provide the desired behavior. For more information, see [Explicit locking in YugabyteDB](../../../explore/transactions/explicit-locking).

## Transactions execution path

End-user statements seamlessly map to one of the types of transactions inside YugabyteDB.

### Single-row transactions

The transaction manager of YugabyteDB automatically detects transactions that update a single row (as opposed to transactions that update rows across tablets or nodes). In order to achieve high performance, the updates to a single row directly update the row without having to interact with the transaction status tablet using a single row transaction path (also known as fast path). For more information, see [Single-row transactions I/O path](../single-row-transactions).

Because single-row transactions do not have to update the transaction status table, their performance is much higher than [distributed transactions](#distributed-transactions).

`INSERT`, `UPDATE`, and `DELETE` single-row SQL statements map to single row transactions.

#### INSERT statements

All single-row `INSERT` statements:

```sql
INSERT INTO table (columns) VALUES (values);
```

#### UPDATE statements

Single-row `UPDATE` statements that specify all primary keys:

```sql
UPDATE table SET column = <new_value> WHERE <all_primary_key_values_are_specified>;
```

Single-row upsert statements using `UPDATE` .. `ON CONFLICT`:

```sql
INSERT INTO table (columns) VALUES (values)
    ON CONFLICT DO UPDATE
    SET <values>;
```

If updates are performed on an existing row, they should match the set of values specified in the `INSERT` clause.

#### DELETE statements

Single-row `DELETE` statements that specify all primary keys:

```sql
DELETE FROM table WHERE <all_primary_key_values_are_specified>;
```

### Distributed transactions

A transaction that impacts a set of rows distributed across multiple tablets (which would be hosted on different nodes in the most general case) use a distributed transactions path to execute transactions. Implementing distributed transactions in YugabyteDB requires the use of a transaction manager that can coordinate various operations included in the transaction and finally commit or abort the transaction as needed. For more information, see [Distributed transactions I/O path](../transactional-io-path).
