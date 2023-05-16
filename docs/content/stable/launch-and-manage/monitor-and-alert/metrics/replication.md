---
title: Replication metrics
headerTitle: Replication
linkTitle: Replication metrics
headcontent: Monitor xCluster replication
description: Learn about YugabyteDB's replication metrics, and how to select and use the metrics.
menu:
  stable:
    identifier: replication
    parent: metrics-overview
    weight: 150
type: docs
---

### xCluster

YugabyteDB allows you to asynchronously replicate data between independent YugabyteDB clusters.

The replication lag metrics are computed at a tablet level as the difference between Hybrid Logical Clock (HLC) time on the source's tablet server, and the hybrid clock timestamp of the latest record pulled from the source.

A description of key metrics in this category is listed in the following table:

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `async_replication_committed_lag_micros` | microseconds | counter | The time in microseconds for the replication lag on the target cluster. This metric is available only on the source cluster. |
| `time_since_last_getchanges` | microseconds | counter | The time elapsed in microseconds from when the source cluster got a request to replicate from the target cluster. This metric is available only on the source cluster. |
| `consumer_safe_time_lag` | microseconds | counter | The time elapsed in microseconds between the physical time and safe time. Safe time is when data has been replicated to all the tablets on the consumer cluster. This metric is available only on the target cluster. |
| `consumer_safe_time_skew` | microseconds | counter | The time elapsed in microseconds for replication between the first and the last tablet replica on the consumer cluster. This metric is available only on the target cluster. |
