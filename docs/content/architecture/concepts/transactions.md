---
title: ACID Transactions
weight: 971
---

YugaByte DB's replication and transaction models are inspired by [Google
Spanner](https://cloud.google.com/spanner/docs/transactions).

## Replication Model

### Strong Consistency at the Core

As detailed in the [Replication](/architecture/concepts/replication/) concepts section, mutations
(such as inserts and updates) in YugaByte DB are replicated using using
[Raft](https://raft.github.io/), a distributed consensus algorithm that is proven to be
theoretically correct under various failure conditions. Google Spanner uses a similar consensus
algorithm called
[Paxos](https://cloud.google.com/spanner/docs/whitepapers/life-of-reads-and-writes). Raft was
[designed](https://ramcloud.stanford.edu/~ongaro/userstudy/) to be easier to understand and hence
easier implement in practice as compared to Paxos. Today, many popular highly reliable distributed
systems including [etcd](https://coreos.com/etcd/) and
[consul](https://www.consul.io/docs/internals/consensus.html) are based on Raft.

Raft is used to achieve strong consistency between replicas when mutations occur. This guarantees
zero data loss if the failures are within the limits tolerated by the system. For example, if
Replication Factor is 5, then YugaByte DB can tolerate failure of up to two nodes without any data
loss. The system doesn't have to resort to a stale copy of the data (unlike eventually consistent or
async replication solutions).

### Tunable Read Consistency

YugaByte DB's read semantics are "strongly consistent" by default. Reads are routed to the leaders
of the Raft group to ensure the latest values are returned. However, an application may override
this setting for individual requests. For example, this is a useful mode if an app can tolerate
slightly stale reads from a follower, for much lower latencies in geo-distributed use cases.

## Transaction Model

### Single Row and Single Shard ACID Transactions

YugaByte DB currently offers ACID semantics for mutations involving a single-row or rows that fall
within the same shard (partition). These mutations incur only one network roundtrip between the
distributed consensus peers.

Even read-modify-write operations within a single row or single shard, such as the following incur
only 1 round trip in YugaByte DB.

```sql
   UPDATE table SET x = x + 1 WHERE ...
   INSERT .. IF NOT EXISTS...
   UPDATE .. IF EXISTS
```

This is unlike Apache Cassandra, which uses a concept called lightweight transactions to achieve
correctness for these read-modify-write operations and incurs [4-network round trip
latency](https://docs.datastax.com/en/cassandra/3.0/cassandra/dml/dmlLtwtTransactions.html).

### Hybrid Time as an MVCC Timestamp

YugaByte DB implements [MVCC](https://en.wikipedia.org/wiki/Multiversion_concurrency_control)
(multiversion concurrency control) and internally keeps track of multiple versions of values
corresponding to the same key, e.g. of a particular column in a particular row. The details of how
multiple versions of the same key are stored in each replica's DocDB are described in the [Encoding
Details](/architecture/concepts/persistence/#encoding-details) section. The last part of each key is
a timestamp, which allows to quickly navigate to a particular version of a key in the RocksDB
key-value store.

The timestamp that we are using for MVCC comes from the [Hybrid
Time](http://users.ece.utexas.edu/~garg/pdslab/david/hybrid-time-tech-report-01.pdf) algorithm, a
distributed timestamp assignment algorithm that combines the advantages of local realtime (physical)
clocks and Lamport clocks.  The Hybrid Time algorithm ensures that events connected by a causal
chain of the form "A happens before B on the same server" or "A happens on one server, which then
sends an RPC to another server, where B happens", always get assigned hybrid timestamps in an
increasing order. This is achieved by propagating a hybrid timestamp with most RPC requests, and
always updating the hybrid time on the receiving server to the highest value seen, including the
current physcial time on the server.  Multiple aspects of YugaByte DB's transaction model rely on
these properties of Hybrid Time, e.g.:

* Hybrid timestamps assigned to committed Raft log entries in the same tablet always keep
  increasing, even if there are leader changes. This is because the new leader always has all
  committed entries from previous leaders, and it makes sure to update its hybrid clock with the
  timestamp of the last committed entry before appending new entries. This property simplifies the
  logic of selecting a safe hybrid time to pick for single-tablet read requests.

* A request trying to read data from a tablet at a particular hybrid time needs to make sure that no
  changes happen in the tablet with timestamps lower than the read timestamp, which could lead to an
  inconsistent result set. The need to read from a tablet at a particular timestamp arises during
  transactional reads across multiple tablets. This condition becomes easier to satisfy due to the
  fact that the read timestamp is chosen as the current hybrid time on the YB-TServer processing the
  read request, so hybrid time on the leader of the tablet we're reading from immediately gets
  updated to a value that is at least as high as than the read timestamp.  Then the read request
  only has to wait for any relevant entries in the Raft queue with timestamps lower than the read
  timestamp to get replicated and applied to RocksDB, and it can proceed with processing the read
  request after that.

### Reading the latest data from a recently elected leader

In a steady state, when the leader is appending and replicating log entries, the latest
majority-replicated entry is exactly the committed one.  However, it is a bit more complicated right
after a leader change.  When a new leader is elected in a tablet, it appends a no-op entry to the
tablet's Raft log and replicates it, as described in the Raft protocol. Before this no-op entry is
replicated, we consider the tablet unavailable for reading up-to-date values and accepting
read-modify-write operations.  This is because the new tablet leader needs to be able to guarantee
that all previous Raft-committed entries are applied to RocksDB and other persisent and in-memory
data structures, and it is only possible after we know that all entries in the new leader's log are
committed.

### Leader leases: deading the latest data in case of a network partition

Leader leases are a mechanism for a tablet leader to establish its authority for a certain short
time period in order to avoid the following inconsistency:

  * The leader is network-partitioned away from its followers
  * A new leader is elected
  * The client writes a new value and the new leader replicates it
  * The client reads a stale value from the old leader.

![leader_leases](/images/leader_leases.svg)

The leader lease mechanism in YugaByte DB works as follows:

* With every leader-to-follower message (AppendEntries in Raft's terminology), whether replicating
  new entries or even an empty heartbeat message, the leader sends a "leader lease" request as a
  **time interval**, e.g.  could be "I want a 2 second lease". The lease duration is usually a
  system-wide parameter. For each peer, the leader also keeps track of the lease expiration time
  corresponding to each pending request (i.e. time when the request was sent + lease duration),
  which is stored in terms of **local monotonic time**
  ([CLOCK_MONOTONIC](https://linux.die.net/man/3/clock_gettime) in Linux).  The leader considers
  itself as a special case of a "peer" for this purpose.  Then, as it receives responses from
  followers, it maintains the majority-replicated watermark of these expiration times **as stored at
  request sending time**. The leader adopts this majority-replicated watermark as its lease
  expiration time, and uses it when deciding whether it can serve consistent read requests or accept
  writes.

* When a follower receives the above Raft RPC, it reads the value of its current **monotonic
  clock**, adds the provided lease interval to that, and remembers this lease expiration time, also
  in terms of its local monotonic time.  If this follower becomes the new leader, it is not allowed
  to serve consistent reads or accept writes until any potential old leader's lease expires.

* To guarantee that any new leader is aware of any old leader's lease expiration, another bit of
  logic is necessary. Each Raft group member records the latest expiration time of an old leader
  that it knows about (in terms of this server's local monotonic time). Whenever a server sends a
  response to a RequestVote RPC, it includes the largest **remaining amount of time** of any known
  old leader's lease with its vote. This is handled similarly to the lease duration from
  AppendEntries on the receiving server: at least this amount of time has to pass since the receipt
  of this request before the recipient can service requests in case it becomes a leader.  This part
  of the algorithm is needed so that we can prove that a new leader will always know about any old
  leader's majority-replicated leader. This is analogous to Raft's correctness proof: there is
  always a server ("the voter") that received a lease request from the old leader and voted for the
  new leader, because the two majorities must overlap.

Note that for this leader lease mechanism, we are not relying on any kind of clock synchronization
for this leader lease implementation, as we're only sending time intervals over the network, and
each server operates in terms of its local monotonic clock. The only two requirements to the clock
implementation are:

* Bounded monotonic clock drift rate between different servers. E.g. if we use the standard Linux
  assumption of less than 500us per second drift rate, we could account for it by multiplying all
  delays mentioned above by 1.001.

* The monotonic clock does not freeze. E.g. if we're running on a VM which freezes temporarily, the
  hypervisor needs to refresh the VM's clock from the hardware clock when it starts running again.

The leader lease mechanism guarantees that at any point in time there is at most one server in any
tablet's Raft group that considers itself to be an up-to-date leader that is allowed to service
consistent reads or accept write requests.

### Distributed ACID Transactions

Distributed ACID transactions involve multi-row as well as multi-shard operations. As a logical next
step, YugaByte DB is working on support for distributed ACID transactions to support operations that
need ACID semantics across mutations to rows or tables that fall on different shards.  This will
allow us to support features like strongly consistent secondary indexes, multi-table/row ACID
operations in both Cassandra (CQL) context as well as in the PostgreSQL context (which is the 3rd
API that YugaByte DB intends to support). More details about the design & implementation of
distributed transactions will be shared on the [YugaByte Community
Forum](https://forum.yugabyte.com/) in the near future.
