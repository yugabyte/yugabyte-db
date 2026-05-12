---
title: YSQL Connection Manager best practices
headerTitle: Best practices
linkTitle: Best practices
description: Best practices
headcontent: How to get the most from YSQL Connection Manager
menu:
  stable:
    identifier: ycm-best-practices
    parent: connection-manager
    weight: 20
type: docs
---

## Reduce stickiness to maximize sharing

Where possible, design application sessions to avoid using session-level state, temporary tables, or settings that require [sticky connections](../ycm-setup/#sticky-connections). Reducing stickiness allows more efficient reuse of server connections across clients.

- Prepared statements: Use protocol-level prepared statements instead of SQL-level prepared statements to avoid stickiness (this may provide better performance than PostgreSQL if in optimized mode).
- Using superuser connections: In some corner cases, providing temporary superuser access to a user on a connection can break the connection after revoking the superuser privileges. If there are no cases of temporarily providing superuser privileges to any user, then you can safely set the [ysql_conn_mgr_superuser_sticky](../ycm-setup/#configure) flag to false.
<!-- (WIP/guarded by a flag) Setting the role or session authorization during a session (SET role/SET session authorization) makes the connection sticky, but this can be disabled by setting <WIP flag name> to false.-->

## Use sticky connections for long-running workloads

Sticky connections are ideal for workloads where avoiding the overhead of connection pooling context switches is important. Currently, you cannot explicitly request a sticky connection through configuration or connection parameters (this capability is planned for a future release).

In the meantime, you can explicitly request sticky connections by using a role with superuser privileges, as all connections initiated by superusers are treated as sticky by default. This approach is particularly recommended for administrative tasks, long-running analytical queries, or debugging sessions where stickiness avoids context switching overhead.

## Coordinate connection scaling using a smart driver

YugabyteDB YSQL [Smart Drivers](/stable/develop/drivers-orms/smart-drivers/) and Connection Manager are designed to work together and complement each other for optimal scalability and performance.

A smart driver intelligently routes connections across nodes in a distributed YugabyteDB cluster, ensuring that application traffic is load-balanced efficiently, and can dynamically route queries to appropriate TServers.

Connection Manager operates at the node level, handling pooling and management of server and client connections in each TServer. It ensures efficient usage of backend resources, reduces the cost of idle connections, and smooths out connection spikes.

By using a smart driver and Connection Manager together, you benefit from end-to-end optimization:

- Connections are intelligently spread across the cluster (Smart Driver)
- In each node, connections are pooled, shared, and throttled effectively (Connection Manager)

This layered architecture enables high concurrency, efficient resource use, and operational simplicity, especially in large-scale, multi-tenant environments.

## Configure optimal memory settings

In a YugabyteDB node, the TServer and PostgreSQL (YSQL) processes run side-by-side and share the memory available on the node.

- The TServer handles data storage, replication (DocDB), and tablet-level operations.
- The PostgreSQL process provides the SQL layer (YSQL), but it's tightly integrated and communicates with the TServer over RPCs.

For optimal distribution of memory between TServer and PostgreSQL processes, set the `use_memory_defaults_optimized_for_ysql` flag to true when you create a cluster. Refer to [Memory division smart defaults](../../../reference/configuration/smart-defaults/#memory-division-smart-defaults) for more details.

Note that when Connection Manager is enabled, an instance of the odyssey process is also run on each database node, which can take up to 200MB of RAM.

## Right-size the cluster

When sizing your YugabyteDB cluster, be sure to use server (actual) connections, not just client connection pool settings, to guide your calculations. Client connection pools may multiplex multiple client connections over a single server one, so it's the server, concurrent active connections that impact resource usage and should drive cluster sizing.

You should create a maximum of 15 server connections per vCPU at a baseline level. However, to right-size your cluster you should also consider the expected number of concurrent active YSQL connections, and the p99 latency requirements of your workload. As the number of active connections increases, so does the cost of CPU context switching, which can negatively impact p99 latencies. For latency-sensitive or high-throughput workloads, it's often better to use fewer than 15 connections per core when calculating your cluster size.

For example, if your workload typically has 600 concurrent active connections, and you assume a safe limit of 10 active connections per vCPU, you'll need at least 60 vCPUs in total. In a 6-node cluster with a replication factor (RF) of 3, the data is evenly distributed, so you can plan for 16 vCPUs per node to meet that requirement. This setup ensures each node has enough compute capacity to handle its share of active connections without introducing excessive latency.

Run performance benchmarks with your specific workload to validate your sizing, and fine-tune the connections-per-core ratio as needed. Keep in mind that you have to think beyond just the number of connections when you're sizing your cluster.
