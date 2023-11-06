---
title: Raft and distributed system metrics
headerTitle: Raft and distributed systems
linkTitle: Raft metrics
headcontent: Monitor raft and distributed system metrics
description: Learn about YugabyteDB's raft and distributed system metrics, and how to select and use the metrics.
menu:
  stable:
    identifier: raft-dst
    parent: metrics-overview
    weight: 130
type: docs
---

## Raft operations, throughput, and latencies

YugabyteDB implements the RAFT consensus protocol, with minor modifications. Replicas implement an RPC method called `UpdateConsensus` which allows a tablet leader to replicate a batch of log entries to the follower. Replicas also implement an RPC method called `RequestConsensusVote`, which candidates invoke to gather votes. The `ChangeConfig` RPC method indicates the number of times a peer was added or removed from the consensus group. An increase in change configuration typically happens when YugabyteDB needs to move data around. This may happen due to a planned server addition or decommission or a server crash looping. A high number for the request consensus indicates that many replicas are looking for a new election because they have yet to receive a heartbeat from the leader. This could happen due to high CPU or a network partition condition.

All handler latency metrics include additional attributes. Refer to [Throughput and latency](../throughput/).

The following are key metrics for monitoring RAFT processing. All metrics are counters in microseconds.

| Metric (Counter \| microseconds) | Description |
| :----- | :--- |
| `handler_latency_yb_consensus_ConsensusService_UpdateConsensus` | Time to replicate a batch of log entries from the leader to the follower. Includes the total count of the RPC method being invoked.
| `handler_latency_yb_consensus_ConsensusService_RequestConsensusVotes` | Time by candidates to gather votes. Includes the total count of the RPC method being invoked.
| `handler_latency_yb_consensus_ConsensusService_ChangeConfig` | Time by candidates to add or remove a peer from the Raft group. Includes the total count of the RPC method being invoked.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_consensus_ConsensusService_UpdateConsensus` | microseconds | counter | The time in microseconds to replicate a batch of log entries from the leader to the follower. This metric includes the total count of the RPC method being invoked. |
| `handler_latency_yb_consensus_ConsensusService_RequestConsensusVotes` | microseconds | counter | The time in microseconds by candidates to gather votes. This metric includes the total count of the RPC method being invoked. |
| `handler_latency_yb_consensus_ConsensusService_ChangeConfig` | microseconds | counter | The time in microseconds by candidates to add or remove a peer from the Raft group. This metric includes the total count of the RPC method being invoked. | -->

The throughput (Ops/Sec) can be calculated and aggregated for nodes across the entire cluster using appropriate aggregations.

## Clock skew

Clock skew is an important metric for performance and data consistency. It signals if the Hybrid Logical Clock (HLC) used by YugabyteDB is out of state or if your virtual machine was paused or migrated. If the skew is more than 500 milliseconds, it may impact the consistency guarantees of YugabyteDB. If there is unexplained, seemingly random latency in query responses and spikes in the clock skew metric, it could indicate that the virtual machine got migrated to another machine, or the hypervisor is oversubscribed.

Clock skew is a gauge in microseconds.

| Metric (Gauge \| microseconds) | Description |
| :--- | :--- |
| `hybrid_clock_skew`| Time for clock drift and skew.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `hybrid_clock_skew` | microseconds | gauge | The time in microseconds for clock drift and skew. | -->

## Remote bootstraps

When a Raft peer fails, YugabyteDB executes an automatic remote bootstrap to create a new peer from the remaining ones. Bootstrapping can also result from planned user activity when adding or decommissioning nodes.

Remote bootstraps is a counter in microseconds.

| Metric (Counter \| microseconds) | Description |
| :--- | :--- |
| `handler_latency_yb_consensus_ConsensusService_StartRemoteBootstrap` | Time to remote bootstrap a new Raft peer. Includes the total count of remote bootstrap connections.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_consensus_ConsensusService_StartRemoteBootstrap` | microseconds | counter | The time in microseconds to remote bootstrap a new Raft peer. This metric includes the total count of remote bootstrap connections. | -->

This metric can be aggregated for nodes across the entire cluster using appropriate aggregations.
