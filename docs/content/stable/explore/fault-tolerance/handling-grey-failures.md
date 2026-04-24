---
title: Handle grey failures
linkTitle: Grey failures
description: Learn how YugabyteDB mitigates partial failures such as slow nodes, intermittent timeouts, and asymmetric connectivity.
headcontent: Mitigate partial failures in YugabyteDB
menu:
  stable:
    identifier: handling-grey-failures
    parent: fault-tolerance
    weight: 45
type: docs
---

A grey failure is a partial failure in which a node, disk, or network path is not completely down, but is unhealthy enough to affect performance or availability.

Examples include:

- High latency
- Intermittent packet loss
- Long GC pauses
- CPU starvation
- Disk stalls
- Asymmetric network connectivity

Grey failures are harder to detect than clean failures because the affected component can still respond some of the time. YugabyteDB mitigates these failures using quorum-based replication, leader election, and topology-aware replica placement.

{{< note title="Note" >}}
YugabyteDB does not use a separate grey-failure subsystem. Instead, it relies on the same fault-tolerance mechanisms that protect against node and network failures.
{{< /note >}}

## What is a grey failure?

In a clean failure, a node is fully unavailable and the cluster can quickly determine that it is down.

In a grey failure, a node may still be partially functional. For example, it may be:

- Reachable from some nodes but not others
- Responding slowly
- Intermittently timing out
- Stalled by CPU, disk, or memory pressure
- Lagging behind while still sending occasional heartbeats

These conditions can increase latency before the system has enough evidence to treat the component as failed.

## How YugabyteDB mitigates grey failures

### Per-tablet Raft replication

YugabyteDB stores data in tablets. Each tablet is replicated across multiple nodes and uses [Raft consensus](https://raft.github.io/).

With a replication factor of 3:

- Each tablet has 3 replicas
- One replica is the leader
- Writes commit after acknowledgement from a majority of replicas

This means a slow or unhealthy follower usually does not prevent progress. As long as the leader can still communicate with a quorum, writes can continue and the lagging follower can catch up later.

For more information, see [Replication](../../../architecture/docdb-replication/).

### Leader election and failover

Every tablet has a single Raft leader that coordinates writes. If the leader becomes too slow, stops sending heartbeats, or loses communication with a majority of replicas, followers can start an election and choose a new leader.

This is especially important when a grey failure affects the current leader:

- If the leader is only slightly degraded, requests may succeed but take longer.
- If the degradation becomes severe enough, heartbeats and RPCs begin to time out.
- Once followers determine that the leader is no longer healthy enough to lead, a new leader is elected.

During the transition, some requests may fail and need to be retried by the client.

### Quorum-based safety

Raft requires a majority of replicas to make progress. This protects against split brain during ambiguous network conditions.

For example, if a leader can communicate with only one of two followers in an RF3 configuration, it no longer has a majority and cannot continue committing writes. A leader in the majority side of the partition can be elected instead.

This does not eliminate latency spikes during a grey failure, but it preserves consistency.

### Master and tablet server heartbeats

Tablet servers send heartbeats to YugabyteDB masters. These heartbeats help the control plane identify nodes that are persistently unhealthy or unavailable.

If a node remains unhealthy long enough, the cluster can respond by:

- Marking the node unavailable
- Re-replicating tablets to other nodes
- Rebalancing load away from the affected node

This is more relevant for prolonged grey failures than for short transient events.

### Topology-aware replica placement

YugabyteDB supports placement across failure domains such as:

- Availability zones
- Racks
- Regions

Spreading replicas across independent failure domains reduces the chance that a grey failure in one host, rack, or AZ affects quorum for a tablet.

For more information, see [Place nodes in zones, regions, and clouds](../../multicloud-deployments/cloud-topologies/).

### Client retries and reconnection

Applications should use retry-capable drivers and connect using multiple nodes where possible. Grey failures can cause transient timeouts or errors during leader changes. Client-side retry logic helps absorb these short disruptions.

## Typical grey-failure scenarios

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

This is one of the most visible grey-failure patterns because all writes for the tablet go through the leader.

### Asymmetric network connectivity

A node can reach some peers but not others.

Typical outcome:

- Quorum rules prevent conflicting leaders from committing writes.
- The side of the partition with a majority can continue after electing a leader.
- The minority side cannot make progress.

### Node under intermittent stalls

A node is alive, but pauses frequently because of disk contention, CPU starvation, or memory pressure.

Typical outcome:

- Requests to tablets led by that node show higher tail latency.
- Leadership may move away if stalls become severe enough.
- If the condition persists, the node may eventually be treated as unavailable.

## Failure timeline: slow leader

The following example shows how YugabyteDB typically handles a grey failure affecting a tablet leader in an RF3 deployment.

### Initial state

Tablet `T1` has three replicas:

- `R1` on node A — leader
- `R2` on node B — follower
- `R3` on node C — follower

All three nodes are healthy. Clients send writes for `T1` to leader `R1`.

### T0: Leader begins to degrade

Node A experiences severe disk latency or CPU stalls.

Effects:

- `R1` still responds, but more slowly.
- Client write latency for `T1` increases.
- Followers still receive some heartbeats, so no election happens yet.

At this stage, the problem looks like slowness rather than failure.

### T1: Timeouts begin

The degradation worsens.

Effects:

- Writes to `R1` begin timing out.
- Heartbeats from `R1` to `R2` and `R3` are delayed or missed.
- Followers start suspecting that the leader is no longer healthy enough to lead.

Clients may start seeing transient errors or timeouts.

### T2: Leader loses authority

`R2` and `R3` stop receiving timely communication from `R1`.

Effects:

- A follower starts a Raft election.
- `R2` or `R3` requests votes from the other replicas.
- One of the healthy followers wins leadership by obtaining a majority.

If node A is badly degraded, it cannot prevent the majority from electing a new leader.

### T3: Leadership changes

Suppose `R2` on node B becomes the new leader.

Effects:

- New writes are routed to `R2`.
- Clients may need to refresh metadata or retry requests.
- Write latency returns closer to normal once traffic reaches the new leader.

At this point, the grey failure has been isolated to the degraded former leader.

### T4: Former leader recovers or remains degraded

Two outcomes are possible:

1. **Node A recovers**

    - `R1` reconnects as a follower.
    - It catches up from the new leader.
    - The tablet returns to a healthy three-replica state.

1. **Node A remains unhealthy**

    - The cluster continues operating with the healthy replicas.
    - If the condition persists, operational remediation or re-replication may be required.

## What to expect during a grey failure

Grey failures are often visible as performance issues before they are visible as hard failures.

Common symptoms include:

- Higher tail latency
- Increased RPC timeouts
- Follower lag
- Temporary write stalls during leadership changes
- Transient client-visible errors that succeed on retry

In most cases, YugabyteDB preserves correctness and eventually restores normal service, but it may take some time for the cluster to distinguish a degraded node from a failed one.

## Best practices

To reduce the impact of grey failures:

- Use a replication factor of 3 or 5.
- Place replicas across multiple AZs or racks.
- Monitor node health, RPC latency, Raft metrics, and follower lag.
- Use client drivers that support retries and reconnection.
- Investigate nodes with recurring stalls, long GC pauses, or storage latency spikes.
- Configure alerting to detect degraded nodes before they become fully unavailable.

## Summary

YugabyteDB mitigates grey failures through:

- Raft quorum replication for each tablet
- Automatic leader election and failover
- Heartbeat-based health detection
- Topology-aware replica placement
- Client retries for transient failures

A degraded follower is usually tolerated with little impact. A degraded leader may temporarily increase latency, but YugabyteDB can elect a healthier leader once the failure is severe enough to be detected.

## Related content

- [Replication](../../../architecture/docdb-replication/)
- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Transactions and consistency](../../../architecture/transactions/)
