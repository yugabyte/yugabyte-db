---
title: Handle gray failures
linkTitle: Gray failures
description: Learn how YugabyteDB mitigates partial failures such as slow nodes, intermittent timeouts, and asymmetric connectivity.
headcontent: Mitigate partial failures in YugabyteDB
menu:
  stable:
    identifier: handling-gray-failures
    parent: fault-tolerance
    weight: 45
type: docs
---

A gray failure is a partial failure in which a node, disk, or network path is not completely down, but is unhealthy enough to affect performance or availability.

Examples include:

- High latency
- Intermittent packet loss
- Long GC pauses
- CPU starvation
- Disk stalls
- Local storage write failures
- Partial or asymmetric [network partitions](../../../architecture/key-concepts/#network-partition)

Gray failures are harder to detect than clean failures because the affected component can still respond some of the time. YugabyteDB mitigates these failures using quorum-based replication, leader election, and topology-aware replica placement.

{{< note title="Note" >}}
YugabyteDB does not use a separate gray-failure subsystem. Instead, it relies on the same fault-tolerance mechanisms that protect against node and network failures.
{{< /note >}}

## What is a gray failure?

In a clean failure, a node is fully unavailable and the cluster can quickly determine that it is down.

In a gray failure, a node may still be partially functional. For example, it may be:

- Reachable from some nodes but not others
- Responding slowly
- Intermittently timing out
- Stalled by CPU, disk, or memory pressure
- Unable to durably write to local storage
- Lagging behind while still sending occasional heartbeats

These conditions can increase latency before the system has enough evidence to treat the component as failed.

A network partition is a communication failure in which parts of the cluster cannot reach each other. A _partial network partition_ is a less clear-cut form in which communication fails only on some paths, in one direction, or intermittently. Partial network partitions are a common type of gray failure.

A local storage write failure is another common type of gray failure. In this case, a node may remain reachable and appear healthy at the process or network level, but it cannot reliably perform durable writes because of storage I/O errors, a full disk, a read-only filesystem, or severe storage latency.

## How YugabyteDB mitigates gray failures

### Per-tablet Raft replication

YugabyteDB stores data in tablets. Each tablet is replicated across multiple nodes and uses [Raft consensus](https://raft.github.io/).

With a replication factor of 3:

- Each tablet has 3 replicas
- One replica is the leader
- Writes commit after acknowledgement from a majority of replicas

This means a slow or unhealthy follower usually does not prevent progress. As long as the leader can still communicate with a quorum and durable replication can continue on a majority of replicas, writes can continue and the lagging or unhealthy follower can catch up later.

For more information, see [Replication](../../../architecture/docdb-replication/).

### Leader election and failover

Every tablet has a single Raft leader that coordinates writes. If the leader becomes too slow, stops sending heartbeats, can no longer make durable write progress, or loses communication with a majority of replicas, followers can start an election and choose a new leader.

This is especially important when a gray failure affects the current leader:

- If the leader is only slightly degraded, requests may succeed but take longer.
- If the degradation becomes severe enough, heartbeats and RPCs begin to time out or write progress stalls.
- Once followers determine that the leader is no longer healthy enough to lead, a new leader is elected.

During the transition, some requests may fail and need to be retried by the client.

### Quorum-based safety

Raft requires a majority of replicas to make progress. This protects against split brain during ambiguous network conditions, including partial network partitions.

For example, if a leader can communicate with only one of two followers in an RF3 configuration, it no longer has a majority and cannot continue committing writes. A leader in the majority side of the partition can be elected instead.

This does not eliminate latency spikes during a gray failure, but it preserves consistency.

### Master and tablet server heartbeats

Tablet servers send heartbeats to YugabyteDB masters. These heartbeats help the control plane identify nodes that are persistently unhealthy or unavailable.

If a node remains unhealthy long enough, the cluster can respond by:

- Marking the node unavailable
- Re-replicating tablets to other nodes
- Rebalancing load away from the affected node

This is more relevant for prolonged gray failures than for short transient events.

{{< note title="Note" >}}
Heartbeats help detect many failures, but a node can still appear alive while suffering from a local storage write failure. In these cases, the node may remain reachable even though it cannot reliably make durable replication progress.
{{< /note >}}

### Topology-aware replica placement

YugabyteDB supports placement across failure domains such as:

- Availability zones
- Racks
- Regions

Spreading replicas across independent failure domains reduces the chance that a gray failure in one host, rack, or AZ affects quorum for a tablet.

For more information, see [Synchronous replication](../../../architecture/docdb-replication/replication/).

### Client retries and reconnection

Applications should use retry-capable drivers and connect using multiple nodes where possible. Gray failures can cause transient timeouts or errors during leader changes. Client-side retry logic helps absorb these short disruptions.

## Typical gray-failure scenarios

### Slow follower

A follower becomes slow because of CPU pressure, disk stalls, or intermittent network delay.

Typical outcome:

- The leader continues writing as long as a quorum is available.
- The slow follower falls behind.
- The follower catches up after the condition improves.

In most cases, this has limited impact on write availability.

### Slow leader

A tablet leader becomes slow but not fully unreachable.

Typical outcome:

- Writes to that tablet experience increased latency.
- Follower replicas continue receiving heartbeats for some time, so failover may not be immediate.
- Once heartbeat or RPC delays exceed failure-detection thresholds, a new election occurs.
- Clients retry failed or timed-out requests.

This is one of the most visible gray-failure patterns because all writes for the tablet go through the leader.

### Local storage write failure

A node remains reachable and continues responding to some RPCs, but it cannot durably write to local storage. This can happen because of storage I/O errors, a full disk, a read-only filesystem, or severe storage latency.

Typical outcome:

- If the affected replica is a follower, the tablet can usually continue making progress as long as a healthy quorum remains available.
- If the affected replica is the leader, writes for tablets led by that node may fail, stall, or time out.
- Because the node can still appear healthy at the network level, detection may take longer than in a clean node failure.
- Once the failure is severe enough to affect Raft progress, leadership can move to a healthy replica.

This is a gray failure because the node is not fully down, but it cannot reliably perform its role in durable replication.

### Partial or asymmetric network partition

A node can communicate with some peers but not others, or communication may fail intermittently or in only one direction.

Typical outcome:

- Quorum rules prevent conflicting leaders from committing writes.
- The side of the partition with a majority can continue after electing a leader.
- The minority side cannot make progress.
- Before failover completes, clients may observe timeouts or higher latency.

A partial network partition can be harder to diagnose than a clean partition because some heartbeats or RPCs may still succeed.

### Node under intermittent stalls

A node is alive, but pauses frequently because of disk contention, CPU starvation, or memory pressure.

Typical outcome:

- Requests to tablets led by that node show higher tail latency.
- Leadership may move away if stalls become severe enough.
- If the condition persists, the node may eventually be treated as unavailable.

## Failure timeline: slow leader during a partial network partition

The following example shows how YugabyteDB typically handles a gray failure affecting a tablet leader in an RF=3 deployment. In this example, the failure is caused by a partial network partition: the leader remains reachable from some peers or clients some of the time, but communication becomes delayed or intermittent.

### Initial state

Tablet `T1` has three replicas:

- `R1` on node A — leader
- `R2` on node B — follower
- `R3` on node C — follower

All three nodes are healthy. Clients send writes for `T1` to leader `R1`.

### T0: Partial network partition begins

A network path affecting node A degrades.

Effects:

- `R1` still responds, but more slowly or inconsistently.
- Some heartbeats and RPCs from `R1` still get through.
- Client write latency for `T1` increases.
- Followers may not immediately start an election because communication has not failed completely.

At this stage, the problem looks like intermittent slowness rather than a clean partition.

### T1: Timeouts begin

The degradation worsens.

Effects:

- Writes to `R1` begin timing out.
- Heartbeats from `R1` to `R2` and `R3` are delayed or missed.
- Replication traffic becomes unreliable.
- Followers start suspecting that the leader is no longer healthy enough to lead.

Clients may start seeing transient errors or timeouts.

### T2: Leader loses authority

`R2` and `R3` stop receiving timely communication from `R1`.

Effects:

- A follower starts a Raft election.
- `R2` or `R3` requests votes from the other replicas.
- One of the healthy followers wins leadership by obtaining a majority.

If node A is isolated from enough peers, it cannot prevent the majority from electing a new leader.

### T3: Leadership changes

Suppose `R2` on node B becomes the new leader.

Effects:

- New writes are routed to `R2`.
- Clients may need to refresh metadata or retry requests.
- Write latency returns closer to normal once traffic reaches the new leader.
- `R1` can no longer make progress as leader because it does not have a quorum.

At this point, the gray failure has been isolated to the degraded former leader and its network path.

### T4: Former leader recovers or remains degraded

Two outcomes are possible:

1. **Node A recovers**

    - `R1` reconnects as a follower.
    - It catches up from the new leader.
    - The tablet returns to a healthy three-replica state.

1. **Node A remains unhealthy**

    - The cluster continues operating with the healthy replicas.
    - If the condition persists, operational remediation or re-replication may be required.

## Failure timeline: leader loses local storage write capability

The following example shows how YugabyteDB typically handles a gray failure affecting a tablet leader in an RF=3 deployment. In this example, the failure is caused by a local storage write problem: the leader remains reachable over the network, but it can no longer reliably persist writes.

### Initial state

Tablet `T2` has three replicas:

- `R1` on node A — leader
- `R2` on node B — follower
- `R3` on node C — follower

All three nodes are healthy. Clients send writes for `T2` to leader `R1`.

### T0: Local storage write failure begins

Node A encounters a storage problem. For example:

- The disk starts returning I/O errors
- The filesystem becomes read-only
- The disk becomes full
- Storage latency increases sharply

Effects:

- `R1` is still reachable over the network.
- Heartbeats and some RPCs may still succeed.
- Client connections to node A may still appear healthy.
- Durable write progress for `T2` slows or stops.

At this stage, the failure can be difficult to detect because the node is still alive and reachable.

### T1: Write progress stalls

Clients continue sending writes to leader `R1`, but `R1` cannot reliably persist them.

Effects:

- Writes to `T2` begin failing, stalling, or timing out.
- Replication for `T2` is disrupted because the leader cannot make normal durable progress.
- Followers may still hear from `R1`, but they do not observe healthy write progress.

From the application perspective, this often appears as increased latency followed by transient write errors.

### T2: Followers detect loss of healthy leadership

As the storage problem persists, `R1` can no longer function effectively as leader.

Effects:

- Heartbeats may become delayed as the node stalls on storage activity.
- Followers stop receiving timely leadership communication or cannot make forward progress with the leader.
- A follower starts a Raft election.
- `R2` or `R3` requests votes from the other replicas.

At this point, the cluster treats the problem as a leadership failure for the affected tablet, even though node A may still be reachable.

### T3: Leadership changes

Suppose `R2` on node B becomes the new leader.

Effects:

- New writes for `T2` are routed to `R2`.
- Clients may need to refresh metadata or retry requests.
- Write latency returns closer to normal once traffic reaches the new leader.
- `R1` can no longer make progress as leader for `T2`.

The failure is now isolated to the replica on node A.

### T4: Former leader recovers or remains degraded

Two outcomes are possible:

1. **Node A recovers**

    - The storage problem is resolved.
    - `R1` rejoins as a follower.
    - It catches up from the new leader.
    - The tablet returns to a healthy three-replica state.

1. **Node A remains unhealthy**

    - The cluster continues operating with the healthy replicas.
    - The affected replica may remain unavailable for the tablet.
    - If the condition persists, operational remediation or re-replication may be required.

## What to expect during a gray failure

Gray failures are often visible as performance issues before they are visible as hard failures.

Common symptoms include:

- Higher tail latency
- Increased RPC timeouts
- Follower lag
- Temporary write stalls during leadership changes
- Transient client-visible errors that succeed on retry
- Storage I/O errors or unusually high storage latency on an otherwise reachable node

In most cases, YugabyteDB preserves correctness and eventually restores normal service, but it may take some time for the cluster to distinguish a degraded node from a failed one.

## Best practices

To reduce the impact of gray failures:

- Use a replication factor of 3 or 5.
- Place replicas across multiple AZs or racks.
- Monitor node health, RPC latency, Raft metrics, and follower lag.
- Monitor storage health, including disk fullness, filesystem state, I/O errors, and persistent storage latency spikes.
- Use client drivers that support retries and reconnection.
- Investigate nodes with recurring stalls, long GC pauses, storage latency spikes, or intermittent network loss.
- Configure alerting to detect degraded nodes before they become fully unavailable.
- Workloads should not rely on the underlying system to detect and mitigate failures. Applications should have their own observability and recovery mechanisms so as to be able to detect and mitigate the impact of gray failures.



## Summary

YugabyteDB mitigates gray failures through:

- Raft quorum replication for each tablet
- Automatic leader election and failover
- Heartbeat-based health detection
- Topology-aware replica placement
- Client retries for transient failures

A degraded follower is usually tolerated with little impact. A degraded leader may temporarily increase latency, especially during a partial network partition or local storage write failure, but YugabyteDB can elect a healthier leader once the failure is severe enough to be detected.

## Related content

- [Replication](../../../architecture/docdb-replication/)
- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Transactions and consistency](../../../architecture/transactions/)
