---
title: Benchmark resilience (fault tolerance)
headerTitle: Resilience
linkTitle: Resilience
description: Benchmark YugabyteDB's ability to withstand component failure.
image: /images/section_icons/explore/high_performance.png
headcontent: Benchmarking the ability to withstand component failure.
menu:
  v2.18:
    identifier: resilience
    parent: benchmark
    weight: 21
type: indexpage
---

Resiliency refers to the ability of a system to withstand and recover from failures or disruptions, whether they are caused by software bugs, hardware issues, network problems, or external events. A resilient system is designed to absorb the impact of failures and continue operating, even if at a degraded level, without experiencing a complete outage.

In YugabyteDB, resiliency is achieved through various techniques, including the following:

- **Fault tolerance**. Replicating tablets on to multiple nodes with one acting as a leader and others as followers. If the leader fails, a new leader is automatically elected, ensuring continuous availability. This replication and fault-tolerant architecture allows the database to withstand the failure of individual nodes, or even entire datacenters without losing data or service availability.
- **Consistency guarantees**. Raft-based consensus ensures full ACID (Atomicity, Consistency, Isolation, Durability) transactions, even across multiple tablets and datacenters. This consistency model helps maintain data integrity and coherence, even in the face of failures or network partitions.
- **Self healing**. YugabyteDB automatically detects and recovers from failures, such as node crashes, disk failures, or network partitions. It can automatically repair and rebalance the cluster by re-replicating data and redistributing tablet leaders to maintain optimal performance and resilience.
- **Elasticity**. YugabyteDB can dynamically adjust the number of replicas and the distribution of data across the cluster, ensuring that the system can handle changes in load and resource requirements. This scalability and elasticity help maintain the overall resilience and availability of the database, even as the workload and infrastructure requirements change over time.
- **Backup and disaster recovery (DR)**. YugabyteDB provides built-in backup and DR capabilities, allowing you to create consistent snapshots of the data and restore it in the event of a major failure or disaster. These backup and DR features help ensure the long-term resilience and recoverability of the database, even in the face of large-scale failures or catastrophic events.

## Jepsen test

[Jepsen](https://jepsen.io/) testing is a methodology and set of tools used to rigorously test the fault tolerance and correctness of distributed systems, particularly databases and other data storage systems.  Jepsen deliberately injects faults into the system, such as network partitions, process crashes, disk failures, and other types of failures.

Jepsen employs a rigorous verification process, which includes generating complex, realistic workloads, carefully monitoring the system's behavior, and analyzing the results to identify any inconsistencies or violations of the specified properties.

YugabyteDB passes 99.9% of the Jepsen tests.

{{<tip>}}
For more details, see [Jepsen test results](jepsen-testing-ysql/).
{{</tip>}}

## Learn more

- [Resiliency, high availability, and fault tolerance](../../explore/fault-tolerance/)
