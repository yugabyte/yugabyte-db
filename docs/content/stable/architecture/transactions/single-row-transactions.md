---
title: Single-row transactions
headerTitle: Single-row transactions
linkTitle: Single-row transactions
description: Learn how YugabyteDB offers ACID semantics for mutations involving a single row or rows that are located within a single shard.
menu:
  stable:
    identifier: architecture-single-row-transactions
    parent: architecture-acid-transactions
    weight: 60
type: docs
---

YugabyteDB offers atomicity, consistency, isolation, durability (ACID) semantics for mutations involving a single row or rows that fall in the same shard (partition, tablet). These mutations incur only one network roundtrip between the distributed consensus peers.

Even read-modify-write operations in a single row or single shard, such as the following, incur only one round trip in YugabyteDB:

```sql
   UPDATE table SET x = x + 1 WHERE ...
   INSERT ... IF NOT EXISTS
   UPDATE ... IF EXISTS
```

Note that this is unlike Apache Cassandra, which uses a concept called lightweight transactions to achieve correctness for these read-modify-write operations and incurs [4-network round trip latency](https://docs.datastax.com/en/cassandra/3.0/cassandra/dml/dmlLtwtTransactions.html).

## Reading the latest data from a recently elected leader

In a steady state, when the leader is appending and replicating log entries, the latest majority-replicated entry is exactly the committed one. However, it becomes more complicated right after a leader change.  When a new leader is elected in a tablet, it appends a no-op entry to the tablet's Raft log and replicates it, as described in the Raft protocol. Before this no-op entry is replicated, the tablet is considered unavailable for reading up-to-date values and accepting read-modify-write operations. This is because the new tablet leader needs to be able to guarantee that all previous Raft-committed entries are applied to RocksDB and other persistent and in-memory data structures, and it is only possible after it is known that all entries in the new leader's log are committed.

## Leader leases: reading the latest data in case of a network partition

Leader leases are a mechanism for a tablet leader to establish its authority for a certain short time period to avoid the following inconsistency:

* The leader is network-partitioned away from its followers.
* A new leader is elected.
* The client writes a new value and the new leader replicates it.
* The client reads a stale value from the old leader.

![A diagram showing a potential inconsistency in case of a network partition if leader leases are not present](/images/architecture/txn/leader_leases_network_partition.svg)

The leader lease mechanism in YugabyteDB prevents this inconsistency, as follows:

* With every leader-to-follower message (`AppendEntries` in Raft's terminology), whether replicating new entries or even an empty heartbeat message, the leader sends a leader lease request as a time interval (for example, "I want a 2-second lease"). The lease duration is usually a system wide parameter. For each peer, the leader also keeps track of the lease expiration time corresponding to each pending request (for example, time when the request was sent plus lease duration), which is stored in terms of what is known as local monotonic time ([CLOCK_MONOTONIC](https://linux.die.net/man/3/clock_gettime) in Linux). The leader considers itself as a special case of a peer for this purpose. Then, as the leader receives responses from followers, it maintains the majority-replicated watermark of these expiration times as stored at request sending time. The leader adopts this majority-replicated watermark as its lease expiration time and uses it when deciding whether it can serve consistent read requests or accept writes.

* When a follower receives the previously described Raft RPC, it reads the value of its current monotonic clock, adds the provided lease interval to that, and remembers this lease expiration time, also in terms of its local monotonic time. If this follower becomes the new leader, it is not allowed to serve consistent reads or accept writes until any potential old leader's lease expires.

* To guarantee that any new leader is aware of any old leader's lease expiration, another bit of logic is necessary. Each Raft group member records the latest expiration time of an old leader that it knows about (in terms of this server's local monotonic time). Whenever a server responds to a `RequestVote` RPC, it includes the largest remaining amount of time of any known old leader's lease in its response. This is handled similarly to the lease duration in a leader's `AppendEntries` request on the receiving server: at least this amount of time has to pass since the receipt of this request before the recipient can service up-to-date requests in case it becomes a leader. This part of the algorithm is needed so that it can be proven that a new leader will always know about any old leader's majority-replicated lease. This is analogous to Raft's correctness proof: there is always a server (the voter) that received a lease request from the old leader and voted for the new leader, because the two majorities must overlap.

  Note that there is no reliance on any kind of clock synchronization for this leader lease implementation, as only time intervals are sent over the network, and each server operates in terms of its local monotonic clock. The following are the only two requirements to the clock implementation:

* Bounded monotonic clock drift rate between different servers. For example, if the standard Linux assumption of less than 500&micro;s per second drift rate is used, it could be accounted for by multiplying all delays mentioned previously by 1.001.

* The monotonic clock does not freeze. For example, if running takes place on a virtual machine which freezes temporarily, the hypervisor needs to refresh the virtual machine's clock from the hardware clock when it starts running again.

The leader lease mechanism guarantees that at any point in time there is at most one server in any tablet's Raft group that considers itself to be an up-to-date leader that is allowed to service consistent reads or accept write requests.

## Safe timestamp assignment for a read request

Every read request is assigned a particular multi-version concurrency control (MVCC) timestamp or hybrid time (for example, called `ht_read`), which allows write operations to the same set of keys to happen in parallel with reads. It is crucial, however, that the view of the database as of this timestamp is not updated by concurrently happening writes. That is, once `ht_read` is selected for a read request, no further writes to the same set of keys can be assigned timestamps earlier than or the same as `ht_read`. As has already been mentioned, strictly increasing hybrid times are assigned to Raft log entries of any given tablet. Therefore, one way to assign `ht_read` safely would be to use the hybrid time of the last committed record. As committed Raft log records are never overwritten by future leaders, and each new leader reads the last log entry and updates its hybrid time, all future records will have strictly later hybrid times.

However, with this conservative timestamp assignment approach, `ht_read` can stay the same if there is no write workload on this particular tablet. This results in a client-observed anomaly if [time-to-live (TTL)](../../../api/ycql/dml_insert/#insert-a-row-with-expiration-time-using-the-using-ttl-clause) is being used: no expired values will disappear, as far as the client is concerned, until a new record is written to the tablet. Then, a lot of old expired values could suddenly disappear. To prevent this anomaly, the read timestamp needs to be assigned to be close to the current hybrid time (which is in its turn close to the physical time) to preserve natural TTL semantics. An attempt should be made to choose `ht_read` to be the latest possible timestamp for which it can be guaranteed that all future write operations in the tablet will have a strictly later hybrid time than that, even across leader changes.

This requires an introduction of a concept of hybrid time leader leases, similarly to absolute-time leader leases discussed previously. With every Raft `AppendEntries` request to a follower, whether it is a regular request or an empty or heartbeat request, a tablet leader computes a hybrid time lease expiration time (for example, called `ht_lease_exp`), and sends that to the follower. `ht_lease_exp` is usually computed as current hybrid time plus a fixed configured duration (for example, 2 seconds). By replying, followers acknowledge the old leader's exclusive authority over assigning any hybrid times up to and including `ht_lease_exp`. Similarly to regular leases, these hybrid time leases are propagated on votes. The leader maintains a majority-replicated watermark and considers itself to have replicated a particular value of a hybrid time leader lease expiration if it sent that or a greater `ht_lease_exp` value to a majority of Raft group members. For this purpose, the leader is always considered to have replicated an infinite leader lease to itself.

### Definition of safe time

Suppose the current majority-replicated hybrid time leader lease expiration is called `replicated_ht_lease_exp`. Then the safe timestamp for a read request can be computed as the maximum of:

* Last committed Raft entry's hybrid time.
* One of the following:
  * If there are uncommitted entries in the Raft log, the minimum of the first uncommitted entry's
    hybrid time - &epsilon;, where &epsilon; is the smallest possible difference in hybrid time and `replicated_ht_lease_exp`.
  * If there are no uncommitted entries in the Raft log, the minimum of the current hybrid time and `replicated_ht_lease_exp`.

In other words, the last committed entry's hybrid time is always safe to read at, but for later hybrid times, the majority-replicated hybrid time leader lease is an upper bound. This is because it can only be guaranteed that no future leader will commit an entry with hybrid time earlier than `ht` if `ht` < `replicated_ht_lease_exp`.

Note that when reading from a single tablet, there is no need to wait for the chosen `ht_read` to become safe to read at because it is chosen as such already. However, if it is decided to read a consistent view of data across multiple tablets, `ht_read` could be chosen on one of them, and there is a need to wait for that timestamp to become safe to read at on the second tablet. This typically happens very quickly, as the hybrid time on the second tablet's leader is instantly updated with the propagated hybrid time from the first tablet's leader, and in the common case there is a need to wait for pending Raft log entries with hybrid times earlier than `ht_read` to be committed.

## Propagating safe time from leader to followers for follower-side reads

YugabyteDB supports reads from followers to satisfy use cases that require an extremely low read latency that can only be achieved by serving read requests in the data center closest to the client. This comes at the expense of potentially slightly stale results, and this is a trade-off that you have to make. Similarly to strongly-consistent leader-side reads, follower-side read operations also have to pick a safe read timestamp.

As stated previously, "safe time to read at" means that no future writes are supposed to change the view of the data as of the read timestamp. However, only the leader is able to compute the safe read time using the algorithm described previously. Therefore, the latest safe time is propagated from leaders to followers on `AppendEntries` RPCs. For example, follower-side reads handled by a partitioned-away follower will see a frozen snapshot of the data, including values with TTL specified not timing out. When the partition is healed, the follower starts receiving updates from the leader and can return read results that would be very close to being up-to-date.
