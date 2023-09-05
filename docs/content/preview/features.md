---
title: YugabyteDB features
headerTitle: YugabyteDB features
linkTitle: Features
description: YugabyteDB Distributed SQL database features.
headcontent: Why YugabyteDB can power your next cloud-based application
type: docs
---

YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a combination of high-performance document store, per-shard distributed consensus replication, and multi-shard ACID transactions (inspired by Google Spanner), YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures, and global data distribution. As a cloud-native database, it can be deployed across public and private clouds as well as in Kubernetes environments.

## Benefits

For cloud-native applications, YugabyteDB provides the following benefits.

### PostgreSQL compatible

Get instantly productive - YugabyteDB reuses PostgreSQL's query layer and supports all advanced features.

- Support for triggers, stored procedures, partial indexes, security, and more
- Build and modernize applications with high agility
- Developers get instantly productive
- Migrate legacy databases to the cloud (Oracle / RAC, SQL Server, DB2)

-> [Explore YugabyteDB's API compatibility](../explore/ysql-language-features/)

### Resilience and continuous availability

Continue without disruption during unplanned infrastructure failures and planned maintenance tasks such as software upgrades and distributed backups.

- Survives multiple failures (nodes, zones, and regions, and data centers)
- Auto-heals with re-replication
- Zero-downtime upgrades and security patching
- Proven in real-world scenarios at scale

-> [Explore YugabyteDB's resilience features](../explore/fault-tolerance/)

### Horizontal scalability

Scale out and in with zero impact.

- Billions of ops/day, hundreds of TB
- Scale queries, storage, and connections by adding nodes
- Reliably scale-out and scale-in on demand with large datasets without impacting running applications

-> [Explore YugabyteDB's scalability](../explore/linear-scalability/)

### Geo-distribution

Use replication and data geo-partitioning to achieve low latency, resilience, and compliance.

- Distribute data across zones and regions with ACID consistency
- Most complete global replication features of any database, including synchronous, asynchronous, geo-partitioning, and hybrid deployments
- Move data closer to customers and comply with regulations like GDPR

-> [Explore YugabyteDB's multi-region capabilities](../explore/multi-region-deployments/)

### Hybrid and multi-cloud

Deploy YugabyteDB anywhere, in any combination - public, private, and hybrid cloud environments, on VMs, containers, or bare metal.

- Avoid vendor lock-in
- Migrate from on-premises data centers to the cloud gradually and with confidence
- Align the specific features and capabilities of different clouds with the requirements of your applications

-> [Explore hybrid- and multi-cloud capabilities](../develop/multi-cloud/)
