---
title: How to get started with YugabyteDB
headerTitle: Resource guide
linkTitle: Resource guide
description: Find resources for getting started with YugabyteDB.
headcontent: Learn more about YugabyteDB with blogs, videos, and more
menu:
  v2.25:
    identifier: get-started-guide
    parent: reference
    weight: 2600
type: docs
unversioned: true
---

Find resources for getting started, migrating existing databases, using your database, and architecting for scalability, reliability, and security.

<!--
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
    <a href="https://inviter.co/yugabytedb" class="orange">
      <img src="/icons/slack.svg"></i>
      Join Us on Slack
    </a>
  </li>

</ul>
-->

## Introducing YugabyteDB

[Distributed SQL 101](https://www.yugabyte.com/distributed-sql/)
: Learn how a distributed SQL architecture achieves higher availability, scalability, and replication.

[How to Scale a Single-Server Database: A Guide to Distributed PostgreSQL](https://www.yugabyte.com/postgresql/distributed-postgresql/)
: Enable seamless scalability, high availability, and global data distribution while preserving PostgreSQL compatibility

[Distributed SQL vs Sharded Postgres](https://www.linkedin.com/pulse/distributed-things-postgresql-franck-pachot-4fr7e/)
: Learn the differences between distributed SQL and sharded Postgres.

[Distributed SQL vs NoSQL vs NewSQL](https://www.linkedin.com/pulse/distributed-sql-architecture-what-oracle-didnt-grasp-franck-pachot-ngghe)
: Learn the differences between distributed SQL and other distributed database implementations.

[How 7 Real-World Customers are Using YugabyteDB Aeon](https://www.yugabyte.com/blog/customers-use-yugabytedb-managed/)
: Real-world use cases for moving to a distributed database.

[Unlocking the Power of Event Streaming with YugabyteDB](https://www.yugabyte.com/blog/companies-use-yugabytedb-event-streaming/)
: Real world use cases for streaming data with YugabyteDB.

[Architecting a Highly Available and Resilient Systems of Record](https://www.youtube.com/watch?v=34n6QSa-_Pc)
: Learn how YugabyteDB can be deployed as a system of record and integrates into the broader data ecosystem.

### Architecture

[Reimagining the RDBMS for the Cloud](https://www.yugabyte.com/blog/reimagining-the-rdbms-for-the-cloud/)
: Learn how YugabyteDB's distributed SQL architecture adapts traditional RDBMS for cloud environments.

[Data Replication in YugabyteDB](https://www.yugabyte.com/blog/data-replication/)
: YugabyteDB's data replication ensures high availability and consistency across distributed nodes with tunable replication strategies.

[Distributed PostgreSQL on a Google Spanner Architecture – Storage Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-storage-layer/)
: Learn about YugabyteDB's distributed Google Spanner-inspired storage layer DocDB.

[Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-query-layer/)
: Learn about YugabyteDB's distributed, highly resilient, PostgreSQL-compatible SQL API layer.

[6 Technical Challenges Developing a Distributed SQL Database](https://www.yugabyte.com/blog/6-technical-challenges-developing-a-distributed-sql-database/)
: Learn how YugabyteDB addresses six key challenges in building a distributed SQL database, including consistency, scalability, and fault tolerance.

## Get started

[How to Start YugabyteDB on Your Laptop](https://www.youtube.com/watch?v=ah_fPDpZjnc)
: Try YugabyteDB out by running it on your laptop in five minutes.

[Get started using Docker](https://github.com/FranckPachot/yugabyted-Compose)
: Use Docker compose to start YugabyteDB with [yugabyted](../../reference/configuration/yugabyted/).

[Create Global Applications](/preview/yugabyte-cloud/managed-labs/)
: Test YugabyteDB features using a demo application in real time.

[Start a multi-node cluster with Yugabyted](../../reference/configuration/yugabyted/#create-a-local-multi-node-cluster)
: Create a multi-node cluster on your laptop.

## Modernize and migrate

[Improving PostgreSQL: How to Overcome the Tough Challenges with YugabyteDB](https://www.yugabyte.com/blog/improve-postgresql/)
: Problem areas in PostgreSQL and how to resolve them in YugabyteDB.

[Database Migration using YugabyteDB Voyager](https://www.youtube.com/playlist?list=PL8Z3vt4qJTkJuqQ2ZH1cnL1yxVEi9swwR)
: Learn how you can migrate databases to YugabyteDB quickly and securely.

[Simplify Database Migration with New YugabyteDB Voyager](https://www.yugabyte.com/blog/simplify-database-migration-voyager/)
: Use YugabyteDB Voyager to migrate from legacy and single-cloud databases.

## Database operations

[Seven Multi-Region Deployment Best Practices](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/)
: Best practices for reducing latencies and improving performance in a multi-region deployment.

[Rapid Data Recovery](https://www.yugabyte.com/blog/rapid-data-recovery-database-amazon-s3/)
: Learn how YugabyteDB performs swift data recovery.

## Develop

[Global Applications with YugabyteDB](https://www.youtube.com/watch?v=jqZxUydBaMQ)
: Explore how to design and build global applications with YugabyteDB.

[Hello world](/preview/develop/tutorials/build-apps/)
: Use your favorite programming language to build an application that uses YSQL or YCQL APIs.

[Distributed PostgreSQL Essentials for Developers: Hands-on Course](https://www.youtube.com/watch?v=rqJBFQ-4Hgk)
: Build a scalable and fault-tolerant movie recommendation service.

[Best practices](/preview/develop/best-practices-develop/)
: Tips and tricks to build applications for high performance and availability.

[Drivers and ORMs](/preview/develop/drivers-orms/)
: Connect applications with your database.

### Hotspots

[How to Avoid Hotspots](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/)
: Avoid hotspots in distributed databases by using application-level sharding to distribute writes across multiple nodes.

[Scalable Range Indexes To Avoid Hotspots on Indexes](https://dev.to/yugabyte/scalable-range-sharding-with-yugabytedb-1o51) (For range queries and Top-n queries)
: Learn how to avoid hotspots on range-based indexes in distributed databases.

### Connection management

[Built-in Connection Manager Turns Key  PostgreSQL Weakness into a Strength](https://www.yugabyte.com/blog/connection-pooling-management/)
: Use YugabyteDB's built-in Connection Manager to improve performance and simplify application architecture.

[Database Connection Management: Exploring Pools and Performance](https://www.yugabyte.com/blog/database-connection-management/)
: Manage database connections effectively, using connection pooling and tuning parameters to reduce latency and handle high concurrency.

[Understanding Client Connections and How Connection Pooling Works in YugabyteDB YSQL](https://www.yugabyte.com/blog/how-connection-pooling-works/)
: Learn about client connections and how you can use third-party connection poolers in YugabyteDB.

## Troubleshooting

[EXPLAIN (ANALYZE, DIST) YugabyteDB distributed execution plan](https://dev.to/franckpachot/explain-analyze-dist-4nlc)
: Use EXPLAIN (ANALYZE, DIST) in YugabyteDB to analyze distributed execution plans.

[Query Tuning How To](/preview/launch-and-manage/monitor-and-alert/query-tuning/)
: Optimize query performance in YugabyteDB, using indexing, query tuning, and tools like EXPLAIN to analyze and improve distributed query execution.

[YugabyteDB Memory Tuning for YSQL](https://www.yugabyte.com/blog/optimizing-yugabytedb-memory-tuning-for-ysql/)
: Optimize YugabyteDB memory for YSQL by tuning TServer and Master processes to prevent out-of-memory issues.

## Indexes

[Distributed Indexes for Optimal Query Performance](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/)
: Design indexes to get the best query performance from a distributed database.

[Boost Queries with Index Only Scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
: Learn how Index Only Scan can boost secondary index queries in a distributed SQL database.

[Covering Indexes](https://dev.to/yugabyte/covering-index-nuances-which-columns-to-cover-where-order-by-limit-select-1f4m)
: Optimize index creation by covering the right columns.

[Secondary Indexes in YugabyteDB](../../explore/ysql-language-features/indexes-constraints/secondary-indexes-ysql/)
: Explore secondary indexes in YugabyteDB using YSQL.

[Range indexes for LIKE queries in YugabyteDB](https://dev.to/yugabyte/range-indexes-for-like-queries-in-yugabytedb-10kd)
: Techniques for performing LIKE queries in YugabyteDB.

### Pagination

[Optimize Pagination in a Distributed Database](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/)
: Design queries to return data with pagination while maintaining global ordering.

[Efficient Pagination in YugabyteDB](https://dev.to/yugabyte/efficient-pagination-in-yugabytedb-postgresql-4h5a)
: Learn about the best indexing pattern for PostgreSQL and YugabyteDB.

[Pagination with an OFFSET is better without OFFSET](https://dev.to/franckpachot/pagination-with-an-offset-is-better-without-offset-5fah)
: Learn why you shouldn't use OFFSET for pagination.

## Geo-Distribution

[Engineering Around the Physics of Latency](https://www.yugabyte.com/blog/geo-distribution-in-yugabytedb-engineering-around-the-physics-of-latency/)
: Learn about the geo-distributed deployment topologies offered by YugabyteDB.

[Local Reads in Geo-Distributed YugabyteDB](https://dev.to/franckpachot/series/18625)
: Perform local reads in geo-distributed YugabyteDB databases.

[Multi-Region Best Practices](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/)
: Techniques to reduce latencies and improve performance in a multi-region deployment.
