---
title: xCluster metrics
headerTitle: xCluster metrics
linkTitle: xCluster
headcontent: Monitor xCluster replication metrics
description: Learn about YugabyteDB's xCluster replication metrics.
menu:
  v2.25:
    identifier: replication
    parent: metrics-overview
    weight: 150
type: docs
---


YugabyteDB allows you to asynchronously replicate data between independent YugabyteDB clusters.

The replication lag metrics are computed at a tablet level as the difference between Hybrid Logical Clock (HLC) time on the source's tablet server, and the hybrid clock timestamp of the latest record pulled from the source.

The following table describes key replication metrics. All metrics are counters and units are microseconds.

| Metric (Counter \| microseconds) | Description |
| :------ | :---------- |
| `async_replication_committed_lag_micros` | The time in microseconds for the replication lag on the target cluster. This metric is available only on the source cluster. |
| `time_since_last_getchanges` | The time elapsed in microseconds from when the source cluster got a request to replicate from the target cluster. This metric is available only on the source cluster. |
| `consumer_safe_time_lag` | The time elapsed in milliseconds between the physical time and safe time. Safe time is when data has been replicated to all the tablets on the consumer cluster. This metric is available only on the target cluster. (Only applies to transactional xCluster.) |
| `consumer_safe_time_skew` | The time elapsed in milliseconds for replication between the first and the last tablet replica on the consumer cluster. This metric is available only on the target cluster. (Only applies to transactional xCluster.) |
