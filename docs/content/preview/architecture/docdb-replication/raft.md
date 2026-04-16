---
title: Raft
headerTitle: Raft consensus protocol
linkTitle: Raft
headContent: Guaranteeing data consistency in a fault-tolerant distributed system
menu:
  preview:
    identifier: architecture-docdb-Raft
    parent: architecture-docdb-replication
    weight: 100
type: docs
---

YugabyteDB replicates data using [Raft](https://en.wikipedia.org/wiki/Raft_(algorithm)) consensus protocol for fault tolerance and providing consistency guarantees.

The authors of Raft, [Diego Ongaro](https://ongardie.net/) and [John Ousterhout](https://web.stanford.edu/~ouster/cgi-bin/home.php), wanted to create a consensus protocol that was simpler and more understandable than the widely-used [Paxos](https://en.wikipedia.org/wiki/Paxos_(computer_science)) protocol. Although the authors chose the name "Raft" when [thinking about logs](https://groups.google.com/g/raft-dev/c/95rZqptGpmU), what can be built using them, and how to escape the island of Paxos, it is common to see Raft expanded as _Replication for Availability and Fault Tolerance_.

YugabyteDB's implementation of Raft is based on [Apache Kudu](https://kudu.apache.org/docs/)'s implementation of Raft but has several enhancements, such as leader leases and pre-voting state during learner mode for correctness, and more.

Let us go over the different components involved in Raft consensus protocol and how they work.

{{<lead link="https://Raft.github.io/">}}
To see an animated visual of the workings of Raft, see [Raft Visualization](https://Raft.github.io/).
{{</lead>}}

## Roles

In Raft, nodes in the distributed system can take on one of three roles.

- **Leader**: The leader is responsible for managing the replicated log and coordinating the other nodes. The Leader continues to send heartbeat messages to maintain its leadership status.
- **Follower**: The leader replicates data in the form of log entries to follower nodes. One of the followers becomes the leader when the leader fails.
- **Candidate**: A follower transitions to the Candidate role to start a new election term and to attempt to become the new leader.

Initially, _all_ nodes start as followers.

## Log entries

Log entries represent the sequence of commands or state changes that need to be replicated and applied across all nodes in the distributed system. Each node maintains its own log, which is an ordered sequence of log entries. Log entries play a fundamental role in achieving consensus and ensuring fault tolerance.

## Log replication

The leader is responsible for replicating log entries to the followers. When a client sends a write request to the leader, the leader appends the request to its local log and then sends the log entry to the followers. The followers then append the log entry to their local logs and send an acknowledgment back to the leader. Once the leader receives a majority of acknowledgments, it considers the log entry to be committed. Followers apply the log entry to their state machine once informed by the leader that the log entry has been committed.

## Replication of the write operation

All write operations are handled by the tablet leader. These are the sequence of operations that happen during the replication of a write.

1. The leader first takes the necessary locks on the key using a local in-memory lock manager and reads data if necessary (for read-modify-write or conditional update operations). Note that this locking mechanism does not exist on followers.
1. The batch of changes to be written to [DocDB](../../docdb/) is prepared. The batch can include writes from many concurrent write requests. The write batch is very close to the final set of RocksDB key-value pairs to be written, only lacking the final [hybrid timestamp](../../transactions/transactions-overview#hybrid-logical-clocks) at the end of each key.
1. The leader appends the batch to its Raft log and chooses a [hybrid timestamp](../../transactions/transactions-overview#hybrid-logical-clocks) as the timestamp for the batch.
1. The data is replicated via Raft to its peers.
1. After receiving a `replication successful` callback from the Raft subsystem, the leader applies the write batch to its local DocDB.
1. At this point, as the log entry has been replicated on a majority of participants, the data is durable. A success acknowledgment is sent to the user.
1. The next update from the leader notifies followers that the entry has been committed, and the followers apply the write batch to their RocksDB instances.

## Leader election

When the system starts up, each node is assigned an initial election term, which is typically a non-negative integer. The term number is persistent and survives restarts. When a follower does not receive a heartbeat in the election timeout period it becomes a candidate to start a new election term and attempt to become the new Leader. Each node votes for a candidate in a particular election term. The candidate that receives votes from a majority of the nodes is elected as the new leader. Whenever a new leader is elected, the election term is incremented. This ensures that each term has a unique identifier, and it helps the nodes in the system differentiate between the various terms.

{{<note>}}
In the scenario where there is no clear majority for a candidate, a new election for the next term is triggered.
{{</note>}}

If a node receives a request from a leader with a stale term (that is, a term that is lower than the node's current term), the node will reject the request and inform the leader of the newer term. This helps prevent outdated leaders from making changes to the replicated log. Raft ensures that the replicated log remains consistent across the nodes by requiring that all log entries be associated with a specific term. Also, when a node comes back online, it will catch up with all the log records it had missed, which can include log records from quite a few different terms.

The leader continues to send heartbeat messages to maintain its leadership status. If a follower does not receive a heartbeat in the election timeout period, a new election is triggered.

## Consistency guarantees

Raft ensures that the replicated log remains consistent across all the nodes in a Raft group. It achieves this by enforcing a rule that the leader can append only new entries to the log, and it must ensure that those entries are consistent with the log of the majority of the participants. If a follower's log is not consistent with the leader's log, the leader will override the follower's log with its own.

## Fault tolerance

Raft is designed to be fault-tolerant, meaning that it can continue to operate even if some of the members of the raft group fail. As long as a majority of the participants in the raft group are alive, the system can continue to make progress and maintain the consistency of the replicated log. If the leader fails, a new leader will be elected, and the system will continue to operate without interruption.

## Learn more

- Original Raft research paper: [In Search of an Understandable Consensus Algorithm](https://Raft.github.io/Raft.pdf)
- [Origin of the name: Raft](https://groups.google.com/g/raft-dev/c/95rZqptGpmU)