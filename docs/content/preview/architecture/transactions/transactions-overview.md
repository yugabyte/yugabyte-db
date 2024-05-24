---
title: Fundamentals of Distributed Transactions
linkTitle: Fundamentals
description: The fundamental concepts behind distributed transactions
menu:
  preview:
    identifier: architecture-transactions-overview
    parent: architecture-acid-transactions
    weight: 100
type: docs
---

A transaction is a sequence of operations performed as a single logical unit of work. The intermediate states of the database as a result of applying the operations inside a transaction are not visible to other concurrent transactions, and if a failure occurs that prevents the transaction from completing, then none of the steps affect the database.

Note that all update operations inside DocDB are considered to be transactions, including operations that update only one row, as well as those that update multiple rows that reside on different nodes. If `autocommit` mode is enabled, each statement is executed as one transaction.

Let us go over some of the fundamental concepts involved in making distributed transactions work.

## Time synchronization

A transaction in a YugabyteDB cluster may need to update multiple rows that span across nodes in a cluster. In order to be ACID-compliant, the various updates made by this transaction should be visible instantaneously as of a fixed time, irrespective of the node in the cluster that reads the update. To achieve this, the nodes of the cluster must agree on a global notion of time, which requires all nodes to have access to a highly-available and globally-synchronized clock. [TrueTime](https://cloud.google.com/spanner/docs/true-time-external-consistency), used by Google Cloud Spanner, is an example of such a clock with tight error bounds. However, this type of clock is not available in many deployments. Physical time clocks (or wall clocks) cannot be perfectly synchronized across nodes and cannot order events with the purpose to establish a causal relationship across nodes.

### Hybrid logical clocks

YugabyteDB uses hybrid logical clocks (HLC) based on the [hybrid time algorithm](http://users.ece.utexas.edu/~garg/pdslab/david/hybrid-time-tech-report-01.pdf), a distributed timestamp assignment algorithm that combines the advantages of local real-time (physical) clocks and Lamport clocks that track causal relationships. The hybrid time algorithm ensures that events connected by a causal chain of the form "A happens before B on the same server" or "A happens on one server, which then sends an RPC to another server, where B happens", always get assigned hybrid timestamps in an increasing order

Each node in a YugabyteDB cluster first computes its HLC represented as a tuple (physical time component, logical component). HLCs generated on any node are strictly monotonic, and are compared as a tuple. When comparing two HLCs, the physical time component takes precedence over the logical component.

* Physical time component: YugabyteDB uses the physical clock (`CLOCK_REALTIME` in Linux) of a node to initialize the physical time component of its HLC. Once initialized, the physical time component can only be updated to a higher value.

* Logical component: For a given physical time component, the logical component of the HLC is a monotonically increasing number that provides ordering of events happening in that same physical time. This is initially set to 0. If the physical time component is updated at any point, the logical component is reset to 0.

On any RPC communication between two nodes, HLC values are exchanged. The node with the lower HLC updates its HLC to the higher value. If the physical time on a node exceeds the physical time component of its HLC, the latter is updated to the physical time and the logical component is set to 0. Thus, HLCs on a node are monotonically increasing.

The same HLC is used to determine the read point in order to determine which updates should be visible to end clients. If an update has safely been replicated onto a majority of nodes, as per the Raft protocol, that update operation can be acknowledged as successful to the client and it is safe to serve all reads up to that HLC. This forms the foundation for [lockless multiversion concurrency control in YugabyteDB](#multi-version-concurrency-control).

### Hybrid time use

Multiple aspects of YugabyteDB's transaction model rely on hybrid time.

Hybrid timestamps assigned to committed [Raft log entries](../../docdb-replication/raft#log-entries) in the same tablet always keep increasing, even if there are leader changes. This is because the new leader always has all committed entries from previous leaders, and it makes sure to update its hybrid clock with the timestamp of the last committed entry before appending new entries. This property simplifies the logic of selecting a safe hybrid time to select for single-tablet read requests.

A request trying to read data from a tablet at a particular hybrid time needs to ensure that no changes happen in the tablet with timestamp values lower than the read timestamp, which could lead to an inconsistent result set. The need to read from a tablet at a particular timestamp arises during transactional reads across multiple tablets. This condition becomes easier to satisfy due to the fact that the read timestamp is chosen as the current hybrid time on the YB-TServer processing the read request, so hybrid time on the leader of the tablet being read from immediately becomes updated to a value that is at least as high as the read timestamp. Then the read request only has to wait for any relevant entries in the Raft queue with timestamp values lower than the read timestamp to be replicated and applied to RocksDB, and it can proceed with processing the read request after that.

### Caveat

The main downside is in certain scenarios where concurrent transactions try to perform conflicting updates. In these scenarios, the conflict resolution depends on the maximum clock skew in the cluster. This leads to a higher number of transaction conflicts or a higher latency of the transaction.

## Multi-version concurrency control

YugabyteDB maintains data consistency internally using multi-version concurrency control (MVCC) without the need to lock rows. Each transaction works on a version of the data in the database as of some hybrid timestamp that is derived from [Hybrid Logical Clock](#hybrid-logical-clocks). This prevents transactions from reading the intermediate updates made by concurrently-running transactions, some of which may be updating the same rows. Each transaction, however, can see its own updates, thereby providing transaction isolation for each database session. Using MVCC minimizes lock contention during the execution of multiple concurrent transactions.

YugabyteDB implements MVCC and internally keeps track of multiple versions of values corresponding to the same key (for example, of a particular column in a particular row), as described in [Persistence on top of RocksDB](../../docdb/persistence). The last part of each key is a timestamp, which enables quick navigation to a particular version of a key in the RocksDB key-value store.

## Provisional records

YugabyteDB needs to store uncommitted values written by distributed transactions in a similar persistent data structure. However, they cannot be written to DocDB as regular values, because they would then become visible at different times to clients reading through different tablet servers, allowing a client to see a partially applied transaction and thus breaking atomicity.

{{<lead link="../distributed-txns#provisional-records">}}
To get a deeper understanding of the layout of the uncommited records, see [Provisional records](../distributed-txns#provisional-records)
{{</lead>}}
