---
title: Benefits of using YugabyteDB
headerTitle: Key benefits
linkTitle: Key benefits
description: Benefits of using YugabyteDB to power your next cloud-native application.
headcontent: Why YugabyteDB should power your next cloud-native application
type: docs
unversioned: true
---

YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a combination of high-performance document store, per-shard distributed consensus replication, and multi-shard ACID transactions (inspired by Google Spanner), YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures, and global data distribution. As a cloud-native database, it can be deployed across public and private clouds as well as in Kubernetes environments.

YugabyteDB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency. Common use cases include:

- Distributed Online Transaction Processing (OLTP) applications needing multi-region scalability without compromising strong consistency and low latency. For example, user identity, Retail product catalog, Financial data service.
- Hybrid Transactional/Analytical Processing (HTAP) (also known as Translytical) applications needing real-time analytics on transactional data. For example, user personalization, fraud detection, machine learning.
- Streaming applications needing to efficiently ingest, analyze, and store ever-growing data. For example, IoT sensor analytics, time series metrics, real-time monitoring.

## Key benefits

YugabyteDB provides five key features for cloud-native applications:

- PostgreSQL compatibility
- Resilience and continuous availability
- Scalability
- Globally distributed
- Hybrid and multi-cloud

### PostgreSQL compatible

Get instantly productive - YugabyteDB reuses PostgreSQL's query layer and supports all advanced features.

- Support for triggers, stored procedures, partial indexes, security, and more
- Build and modernize applications with high agility
- Developers are instantly productive
- Migrate legacy databases to the cloud (Oracle / RAC, SQL Server, DB2)

--> [Explore YugabyteDB's API compatibility](../explore/ysql-language-features/)

### Resilience and continuous availability

Continue without disruption during unplanned infrastructure failures and planned maintenance tasks such as software upgrades and distributed backups.

- Survives multiple failures (nodes, zones, regions, and data centers)
- Auto-heals with re-replication
- Zero-downtime upgrades and security patching
- Proven in real-world scenarios at scale

--> [Explore resilience and continuous availability](../explore/fault-tolerance/)

### Horizontal scalability

Scale out and in with zero impact.

- Billions of ops/day, hundreds of TB
- Scale queries, storage, and connections by adding nodes
- Reliably scale-out and scale-in on demand with large datasets without impacting running applications

--> [Explore scalability](../explore/linear-scalability/)

### Geo-distribution

Use replication and data geo-partitioning to achieve low latency, resilience, and compliance.

- Distribute data across zones and regions with ACID consistency
- Most complete global replication features of any database, including synchronous, asynchronous, geo-partitioning, and hybrid deployments
- Move data closer to customers and comply with regulations like GDPR

--> [Explore multi-region deployments](../explore/multi-region-deployments/)

### Hybrid and multi-cloud

Deploy YugabyteDB anywhere, in any combination - public, private, and hybrid cloud environments, on VMs, containers, or bare metal.

- Avoid vendor lock-in
- Migrate from on-premises data centers to the cloud gradually and with confidence
- Align the specific features and capabilities of different clouds with the requirements of your applications

--> [Explore hybrid- and multi-cloud capabilities](../develop/multi-cloud/)

## Next steps

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Quick start"
    description="Create your first cluster, explore distributed SQL, and build a sample application in 15 minutes."
    buttonText="Get Started"
    buttonUrl="../quick-start-yugabytedb-managed/"
  >}}

  {{< sections/bottom-image-box
    title="Explore YugabyteDB"
    description="Test YugabyteDB's features through examples including API compatibility, availability, scalability, and more."
    buttonText="Explore"
    buttonUrl="../explore/"
  >}}
{{< /sections/2-boxes >}}
