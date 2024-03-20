---
title: How to get started with YugabyteDB
headerTitle: How to get started with YugabyteDB
linkTitle: How to get started
description: Benefits of using YugabyteDB to power your next cloud-native application.
headcontent: Find resources for getting started with YugabyteDB
type: docs
unversioned: true
---

Find resources for getting started, migrating existing databases, using your database, and architecting for scalability, reliability, and security.

## About YugabyteDB

**Video**: [Introducing YugabyteDB: The Distributed SQL Database for Mission-Critical Applications](https://www.youtube.com/watch?v=j24p07Frw00)

The only distributed SQL database in the world that's open source, PostgreSQL compatible, and can run across hybrid and multi-cloud environments

**Article**: [Distributed SQL 101](https://www.yugabyte.com/distributed-sql/)

What is Distributed SQL and why you need it for your database modernization initiatives and cloud native applications.

## Getting started



## Migrating from PostgreSQL

**Video**: [Database Migration using YugabyteDB Voyager](https://www.youtube.com/watch?v=GEzs---fFPQ&list=PL8Z3vt4qJTkJuqQ2ZH1cnL1yxVEi9swwR)

**Blog**: [Simplify Database Migration with New YugabyteDB Voyager](https://www.yugabyte.com/blog/simplify-database-migration-voyager/)

Manage the complete database migration lifecycle to simplify and automate data migration from legacy and single-cloud databases to YugabyteDB.

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
