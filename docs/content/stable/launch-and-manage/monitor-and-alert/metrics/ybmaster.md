---
title: YB-Master metrics
headerTitle: YB-Master metrics
linkTitle: YB-Master metrics
headcontent: Monitor table and tablet operations
description: Learn about YugabyteDB's YB-Master metrics, and how to select and use the metrics.
menu:
  stable:
    identifier: yb-master-metrics
    parent: metrics-overview
    weight: 140
type: docs
---

The [YB-Master](../../../../architecture/concepts/yb-master/) hosts system metadata, records about tables in the system and locations of their tablets, users, roles, permissions, and so on. YB-Masters are also responsible for coordinating background operations such as schema changes, handling addition and removal of nodes from the cluster, automatic re-replication of data on permanent failures, and so on.

All handler latency metrics include additional attributes. Refer to [Throughput and latency](../throughput/).

The following are key metrics for evaluating YB-Master performance.

| Metric | Unit | Type | Description |
| :--- | :--- | :--- | :--- |
| `handler_latency_yb_master_MasterClient_GetTabletLocations` | microseconds | counter | Time spent on fetching the replicas from the master servers. This metric includes the number of times the locations of the replicas are fetched from the master server.
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | Time to read the PostgreSQL system tables (during DDL). This metric includes the count or number of reads.
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | Time to write the PostgreSQL system tables (during DDL). This metric includes the count or number of writes.
| `handler_latency_yb_master_MasterDdl_CreateTable` | microseconds | counter | Time to create a table (during DDL). This metric includes the count of create table operations.
| `handler_latency_yb_master_MasterDdl_DeleteTable` | microseconds | counter | Time to delete a table (during DDL). This metric includes the count of delete table operations.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_master_MasterClient_GetTabletLocations` | microseconds | counter | The number of microseconds spent on fetching the replicas from the master servers. This metric includes the number of times the locations of the replicas are fetched from the master server. |
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | The time in microseconds to read the PostgreSQL system tables (during DDL). This metric includes the count or number of reads. |
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | The time in microseconds to write the PostgreSQL system tables (during DDL). This metric includes the count or number of writes. |
| `handler_latency_yb_master_MasterDdl_CreateTable` | microseconds | counter | The time in microseconds to create a table (during DDL). This metric includes the count of create table operations.|
| `handler_latency_yb_master_MasterDdl_DeleteTable` | microseconds | counter | The time in microseconds to delete a table (during DDL). This metric includes the count of delete table operations.| -->

These metrics can be aggregated for nodes across the entire cluster using appropriate aggregations.
