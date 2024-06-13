<!---
title: How to get started with YugabyteDB
headerTitle: How to get started with YugabyteDB
linkTitle: Guide to resources
description: Find resources for getting started with YugabyteDB.
headcontent: Find resources for getting started with YugabyteDB
type: docs
unversioned: true
private: true
--->

Find resources for getting started, migrating existing databases, using your database, and architecting for scalability, reliability, and security.

<ul class="nav yb-pills">

  <li>
    <a href="https://cloud.yugabyte.com/signup" class="orange">
      <img src="/icons/yugabyte.svg"></i>
      Sign Up
    </a>
  </li>
  <li>
    <a href="https://download.yugabyte.com/" class="orange">
      <img src="/icons/database.svg"></i>
      Download Now
    </a>
  </li>
  <li>
    <a href="https://communityinviter.com/apps/yugabyte-db/register" class="orange">
      <img src="/icons/slack.svg"></i>
      Join Us on Slack
    </a>
  </li>

</ul>

## Introducing YugabyteDB

**Video**: [Introducing YugabyteDB: The Distributed SQL Database for Mission-Critical Applications](https://www.youtube.com/watch?v=j24p07Frw00)
: Learn about YugabyteDB and how it supports mission-critical applications.

**Documentation**: [Key benefits](../features/)
: Why YugabyteDB should power your next cloud-native application.

**Article**: [Distributed SQL 101](https://www.yugabyte.com/distributed-sql/)
: How Distributed SQL powers database modernization and cloud-native applications.

**Article**: [How to Scale a Single-Server Database: A Guide to Distributed PostgreSQL](https://www.yugabyte.com/postgresql/distributed-postgresql/)
: Why you need Distributed PostgreSQL to truly scale.

**Article**: [Distributed SQL vs Sharded Postgres](https://www.linkedin.com/pulse/distributed-things-postgresql-franck-pachot-4fr7e/)<br>
**Article**: [Distributed SQL vs NoSQL vs NewSQL](https://www.linkedin.com/pulse/distributed-sql-architecture-what-oracle-didnt-grasp-franck-pachot-ngghe)
: Learn the difference between distributed SQL and other strategies for scaling.

**Blog**: [How 7 Real-World Customers are Using YugabyteDB Managed](https://www.yugabyte.com/blog/customers-use-yugabytedb-managed/)
: Real-world use cases for moving to a distributed database.

**Video**: [Global Applications with YugabyteDB](https://www.youtube.com/watch?v=jqZxUydBaMQ)<br>
**Documentation**: [Build Global Apps](../develop/build-global-apps/)
: Explore how to design and build global applications with YugabyteDB.

**Blog**: [Unlocking the Power of Event Streaming with YugabyteDB](https://www.yugabyte.com/blog/companies-use-yugabytedb-event-streaming/)
: Real world use cases for streaming data with YugabyteDB.

**Video**: [Architecting a Highly Available and Resilient Systems of Record](https://www.youtube.com/watch?v=34n6QSa-_Pc)
: Learn how YugabyteDB can be deployed as a system of record and integrates into the broader data ecosystem.

### Features and architecture

**Documentation**: [Resiliency, high availability, and fault tolerance](../explore/fault-tolerance/)
: Learn how YugabyteDB survives and recovers from outages.

**Blog**: [Data Replication in YugabyteDB](https://www.yugabyte.com/blog/data-replication/)
: Learn the different data replication options available with YugabyteDB.

## Get started

**Video**: [How to Start YugabyteDB on Your Laptop](https://www.youtube.com/watch?v=ah_fPDpZjnc)
: Try YugabyteDB out by running it on your laptop in five minutes.

**Product Lab**: [Create Global Applications](/preview/yugabyte-cloud/managed-labs/)
: Test YugabyteDB features using a demo application in real time.

**Documentation**: [Quick Start](/preview/quick-start-yugabytedb-managed/)
: Try it out for free, in the cloud or on your laptop.

**Documentation**: [Start a multi-node cluster with Yugabyted](../reference/configuration/yugabyted/#create-a-local-multi-node-cluster)
: Create a multi-node cluster on your laptop.

**Documentation**: [Explore YugabyteDB](../explore/)
: Learn about YugabyteDB features, with examples.

**Article**: [Get started using Docker](https://github.com/FranckPachot/yugabyted-Compose)
: Use Docker compose to start YugabyteDB with yugabyted.

## Migrate

**Playlist**: [Database Migration using YugabyteDB Voyager](https://www.youtube.com/playlist?list=PL8Z3vt4qJTkJuqQ2ZH1cnL1yxVEi9swwR)
: Learn how you can migrate databases to YugabyteDB quickly and securely.

**Blog**: [Simplify Database Migration with New YugabyteDB Voyager](https://www.yugabyte.com/blog/simplify-database-migration-voyager/)
: Use YugabyteDB Voyager to migrate from legacy and single-cloud databases.

**Documentation**: [YugabyteDB Voyager](/preview/yugabyte-voyager/)
: Simplify migration from legacy and cloud databases to YugabyteDB.

**Blog**: [Improving PostgreSQL: How to Overcome the Tough Challenges with YugabyteDB](https://www.yugabyte.com/blog/improve-postgresql/)
: Problem areas in PostgreSQL and how to resolve them in YugabyteDB.

## Database operations

**Blog**: [Seven Multi-Region Deployment Best Practices](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/)
: Learn how to reduce latencies and improve performance in a multi-region deployment.

**Documentation**: [Deploy YugabyteDB](../deploy/manual-deployment/)
: Deploy to the public cloud, a private data center, or Kubernetes.

**Blog**: [Rapid Data Recovery](https://www.yugabyte.com/blog/rapid-data-recovery-database-amazon-s3/)
: Learn how YugabyteDB performs swift data recovery.

**Documentation**: [Backup and restore](../manage/backup-restore/)
: Backup, restore, and point-in-time-recovery.

**Documentation**: [Upgrade YugabyteDB](../manage/upgrade-deployment/)
: Upgrade a deployment.

## Develop

**Documentation**: [Hello world](/preview/tutorials/build-apps/)
: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.

**Video**: [Distributed PostgreSQL Essentials for Developers: Hands-on Course](https://www.youtube.com/watch?v=rqJBFQ-4Hgk)<br>
**Documentation**: [Build and learn the essential capabilities of YugabyteDB](/preview/tutorials/build-and-learn/)
: Build a scalable and fault-tolerant movie recommendation service.

**Documentation**: [Best practices](../drivers-orms/)
: Tips and tricks to build applications for high performance and availability.

**Documentation**: [Drivers and ORMs](../drivers-orms/)
: Connect applications with your database.

### Connections

**Blog**: [Database Connection Management: Exploring Pools and Performance](https://www.yugabyte.com/blog/database-connection-management/)
: Database connection management with YugabyteDB.

**Blog**: [Understanding Client Connections and How Connection Pooling Works in YugabyteDB YSQL](https://www.yugabyte.com/blog/how-connection-pooling-works/)
: Understand client connections in YugabyteDB, and how connection pooling helps.

## Troubleshooting

**Article**: [Explain Analyze Dist](https://dev.to/franckpachot/explain-analyze-dist-4nlc)
: YugabyteDB distributed execution plans.

**Documentation**: [Query Tuning How To](../explore/query-1-performance/)
: Query tuning in YugabyteDB.

**Blog**: [YugabyteDB Memory Tuning for YSQL](https://www.yugabyte.com/blog/optimizing-yugabytedb-memory-tuning-for-ysql/)
: Best practices for tuning memory with a YSQL workload.

## Indexes

**Blog**: [Distributed Indexes for Optimal Query Performance](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/)
: Design indexes to get the best query performance from a distributed database.

**Blog**: [Boost Queries with Index Only Scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
: Learn how Index Only Scan can boost secondary index queries in a distributed SQL database.

**Article**: [Covering Indexes](https://dev.to/yugabyte/covering-index-nuances-which-columns-to-cover-where-order-by-limit-select-1f4m)
: Optimize index creation by covering the right columns.

**Documentation**: [Secondary Indexes in YugabyteDB](../explore/ysql-language-features/indexes-constraints/secondary-indexes-ysql/)
: Explore secondary indexes in YugabyteDB using YSQL.

**Article**: [Range indexes for LIKE queries in YugabyteDB](https://dev.to/yugabyte/range-indexes-for-like-queries-in-yugabytedb-10kd)
: Techniques for performing LIKE queries in YugabyteDB.

### Pagination

**Blog**: [Optimize Pagination in a Distributed Database](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/)
: Design queries to return data with pagination while maintaining global ordering.

**Article**: [Efficient Pagination in YugabyteDB](https://dev.to/yugabyte/efficient-pagination-in-yugabytedb-postgresql-4h5a)
: Learn about the best indexing pattern for PostgreSQL and YugabyteDB.

**Article**: [Pagination with an OFFSET is better without OFFSET](https://dev.to/franckpachot/pagination-with-an-offset-is-better-without-offset-5fah)
: Learn why you shouldn't use OFFSET for pagination.

## Hotspots

**Blog**: [How to Avoid Hotspots](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/)<br>
**Article**: [Scalable Range Indexes To Avoid Hotspots on Indexes](https://dev.to/yugabyte/scalable-range-sharding-with-yugabytedb-1o51) (For range queries and Top-n queries)
: Learn how to avoid hotspots on range-based indexes in distributed databases.

## Geo-Distribution

**Blog**: [Engineering Around the Physics of Latency](https://www.yugabyte.com/blog/geo-distribution-in-yugabytedb-engineering-around-the-physics-of-latency/)
: Learn about the geo-distributed deployment topologies offered by YugabyteDB.

**Article**: [Local Reads in Geo-Distributed YugabyteDB](https://dev.to/franckpachot/series/18625)
: Perform local reads in geo-distributed YugabyteDB databases.

**Blog**: [Multi-Region Best Practices](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/)
: Techniques to reduce latencies and improve performance in a multi-region deployment.
