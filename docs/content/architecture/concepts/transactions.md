---
title: ACID Transactions
weight: 971
---

YugaByte DB's replication and transaction models are inspired by [Google
Spanner](https://cloud.google.com/spanner/docs/transactions).

## Replication model

### Strong consistency at the core

As detailed in the [Replication](/architecture/concepts/replication/) section, mutations (such as
inserts and updates) in YugaByte DB are replicated using using [Raft](https://raft.github.io/), a
distributed consensus algorithm that is proven to be theoretically correct under various failure
conditions. Google Spanner uses a similar consensus algorithm called
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

### Tunable read consistency

YugaByte DB's read semantics are "strongly consistent" by default. Reads are routed to the leaders
of the Raft group to ensure the latest values are returned. However, an application may override
this setting for individual requests. For example, this is a useful mode if an app can tolerate
slightly stale reads from a follower, in exchange for much lower latencies in geo-distributed use
cases.

## Transaction model

### Single row and single shard ACID transactions

YugaByte DB currently offers ACID semantics for mutations involving a single row or rows that fall
within the same shard (partition, tablet). These mutations incur only one network roundtrip between
the distributed consensus peers.

Even read-modify-write operations within a single row or single shard, such as the following incur
only one round trip in YugaByte DB.

```sql
   UPDATE table SET x = x + 1 WHERE ...
   INSERT ... IF NOT EXISTS
   UPDATE ... IF EXISTS
```

This is unlike Apache Cassandra, which uses a concept called lightweight transactions to achieve
correctness for these read-modify-write operations and incurs [4-network round trip
latency](https://docs.datastax.com/en/cassandra/3.0/cassandra/dml/dmlLtwtTransactions.html).

### Hybrid time as an MVCC timestamp

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

### Leader leases: reading the latest data in case of a network partition

Leader leases are a mechanism for a tablet leader to establish its authority for a certain short
time period in order to avoid the following inconsistency:

  * The leader is network-partitioned away from its followers
  * A new leader is elected
  * The client writes a new value and the new leader replicates it
  * The client reads a stale value from the old leader.

![A diagram showing a potential inconsistency in case of a network partition if leader leases are
not present](/images/txn/leader_leases_network_partition.svg)

The leader lease mechanism in YugaByte prevents this inconsistency. It works as follows:

* With every leader-to-follower message (AppendEntries in Raft's terminology), whether replicating
  new entries or even an empty heartbeat message, the leader sends a "leader lease" request as a
  **time interval**, e.g.  could be "I want a 2-second lease". The lease duration is usually a
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
  that it knows about (in terms of this server's local monotonic time). Whenever a server responds
  to a RequestVote RPC, it includes the largest **remaining amount of time** of any known old
  leader's lease in its response. This is handled similarly to the lease duration in a leader's
  AppendEntries request on the receiving server: at least this amount of time has to pass since the
  receipt of this request before the recipient can service up-to-date requests in case it becomes a
  leader.  This part of the algorithm is needed so that we can prove that a new leader will always
  know about any old leader's majority-replicated lease. This is analogous to Raft's correctness
  proof: there is always a server ("the voter") that received a lease request from the old leader
  and voted for the new leader, because the two majorities must overlap.

Note that we are not relying on any kind of clock synchronization for this leader lease
implementation, as we're only sending time intervals over the network, and each server operates in
terms of its local monotonic clock. The only two requirements to the clock implementation are:

* Bounded monotonic clock drift rate between different servers. E.g. if we use the standard Linux
  assumption of less than 500us per second drift rate, we could account for it by multiplying all
  delays mentioned above by 1.001.

* The monotonic clock does not freeze. E.g. if we're running on a VM which freezes temporarily, the
  hypervisor needs to refresh the VM's clock from the hardware clock when it starts running again.

The leader lease mechanism guarantees that at any point in time there is at most one server in any
tablet's Raft group that considers itself to be an up-to-date leader that is allowed to service
consistent reads or accept write requests.

### Safe timestamp assignment for a read request

Every read request is assigned a particular MVCC timestamp / hybrid time (let's call it
**ht_read**), which allows write operations to the same set of keys to happen in parallel with
reads. It is crucial, however, that the view of of the database *as of this timestamp* is not
updated by concurrently happening writes. In other words, once we've picked **ht_read** for a read
request, no further writes to the same set of keys can be assigned timestamps lower than or equal to
**ht_read**. As we mentioned above, we assign strictly increasing hybrid times to Raft log entries
of any given tablet.  Therefore, one way to assign **ht_read** safely would be to use the hybrid
time of the last committed record. As committed Raft log records are never overwritten by future
leaders, and each new leader reads the last log entry and updates its hybrid time, all future
records will have strictly higher hybrid times.

However, with this conservative timestamp assignment approach, **ht_read** can stay the same if
there is no write workload on this particular tablet. This will result in a client-observed anomaly
if [TTL
(time-to-live)](/api/cassandra/dml_insert/#insert-a-row-with-expiration-time-using-the-using-ttl-clause)
is being used: no expired values will disappear, as far as the client is concerned, until a new
record is written to the tablet. Then, a lot of old expired values could suddenly disappear. To
combat this anomaly, we need to assign the read timestamp to be close to the current hybrid time
(which is in its turn close to the physical time) to preserve natural TTL semantics. We should
therefore try to choose **ht_read** to be the *highest possible timestamp* for which we can
guarantee that all future write operations in the tablet will have a strictly higher hybrid time
than that, even across leader changes.

For this, we need to introduce a concept of "hybrid time leader leases", similar to absolute-time
leader leases discussed in the previous section. With every Raft AppendEntries request to a
follower, whether it is a regular request or an empty / heartbeat request, a tablet leader computes
a "hybrid time lease expiration time", or **ht_lease_exp** for short, and sends that to the
follower. **ht_lease_exp** is usually computed as current hybrid time plus a fixed configured
duration (e.g. 2 seconds). By replying, followers acknoweledge the old leader's exclusive authority
over assigning any hybrid times up to and including **ht_lease_exp**. Similarly to regular leases,
these hybrid time leases are propagated on votes. The leader maintains a majority-replicated
watermark, and considers itself to have replicated a particular value of a hybrid time leader lease
expiration if it sent that or a higher **ht_lease_exp** value to a majority of Raft group members.
For this purpose, the leader is always considered to have replicated an infinite leader lease to
itself.

#### Definition of safe time
Now, suppose the current majority-replicated hybrid time leader lease expiration is
**replicated_ht_lease_exp**. Then the safe timestamp for a read request can be computed as the
maximum of:

  * Last committed Raft entry's hybrid time
  * One of:
    - If there are uncommitted entries in the Raft log: the minimum of the first uncommitted entry's
      hybrid time - &epsilon; (where &epsilon; is the smallest possible difference in hybrid time)
      and **replicated_ht_lease_exp**.
    - If there are no uncommitted entries in the Raft log: the minimum of the current hybrid time
      and **replicated_ht_lease_exp**.

In other words, the last committed entry's hybrid time is always safe to read at, but for higher
hybrid times, the majority-replicated hybrid time leader lease is an upper bound. That is because we
can only guarantee that no future leader will commit an entry with hybrid time less than **ht** if
**ht < replicated_ht_lease_exp**.

Note that when reading from a single tablet, we never have to wait for the chosen **ht_read** to
become safe to read at because it is chosen as such already. However, if we decide to read a
consistent view of data across multiple tablets, **ht_read** could be chosen on one of them, and
we'll have to wait for that timestamp to become safe to read at on the second tablet. This will
typically happen very quickly, as the hybrid time on the second tablet's leader will be instantly
updated with the propagated hybrid time from the first tablet's leader, and in the common case we
will just have to wait for pending Raft log entries with hybrid times less than **ht_read** to be
committed.

### Isolation levels

YugaByte DB currently implements [Snapshot
Isolation](https://en.wikipedia.org/wiki/Snapshot_isolation), also known as SI, which is an
transaction isolation level that guarantees that all reads made in a transaction will see a
consistent snapshot of the database, and the transaction itself will successfully commit only if no
updates it has made conflict with any concurrent updates made by transactions that committed since
that snapshot.  We are also working on supporting the
[Serializable](https://en.wikipedia.org/wiki/Isolation_(database_systems)#Serializable) isolation
level, which would by definition guarantee that transactions run in a way equivalent to a serial
(sequential) schedule.

In order to support these two isolation levels, the lock manager internally supports three types
of locks:

  - **Snapshot Isolation Write Lock**. This type of a lock is taken by a snapshot isolation
    transaction on values that it modifies.
  - **Serializable Read Lock**. This type of a lock is taken by serializable read-modify-write
    transactions on values that they read in order to guarantee they are not modified until the
    transaciton commits.
  - **Serializable Write Lock**. This type of a lock is taken by serializable transactions on values
    they write, as well as by pure-write snapshot isolation transactions. Multiple snapshot-isolation
    transactions writing the same item can thus proceed in parallel.

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
