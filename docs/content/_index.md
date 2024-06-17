---
title: YB Documentation
headerTitle: Build scalable applications and learn distributed SQL
description: Guides, examples, and reference material you need to evaluate YugabyteDB database, build scalable applications, and learn distributed SQL.
type: indexpage
layout: list
breadcrumbDisable: true
weight: 1
showRightNav: true
resourcesIntro: Products
resources:
  - title: Open source
    url: /preview/
  - title: Fully managed
    url: /preview/yugabyte-cloud/
  - title: Enterprise DBaaS
    url: /preview/yugabyte-platform/
  - title: Migrate from other RDBMS
    url: /preview/yugabyte-voyager/
---

YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a combination of high-performance document store, per-shard distributed consensus replication, and multi-shard ACID transactions, YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures, and global data distribution. As a cloud-native database, it can be deployed across public and private clouds as well as in Kubernetes environments.

YugabyteDB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency.

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started in the Cloud"
    description="Create your first cluster, explore distributed SQL, and build a sample application in 15 minutes. No credit card required."
    buttonText="Get started"
    buttonUrl="/preview/yugabyte-cloud/cloud-quickstart/"
    imageAlt="Yugabyte cloud" imageUrl="/images/homepage/yugabyte-in-cloud.svg"
  >}}

  {{< sections/bottom-image-box
    title="Get Started locally on your Laptop"
    description="Download and install YugabyteDB on your laptop to create clusters, test features, and explore distributed SQL."
    buttonText="Get started"
    buttonUrl="/preview/quick-start/"
    imageAlt="Locally Laptop" imageUrl="/images/homepage/locally-laptop.svg"
  >}}
{{< /sections/2-boxes >}}

## Introduction to YugabyteDB

##### Video: [Introducing YugabyteDB: The Distributed SQL Database for Mission-Critical Applications](https://www.youtube.com/watch?v=j24p07Frw00)

Learn about YugabyteDB and how it supports mission-critical applications.

##### Article: [How to Scale a Single-Server Database: A Guide to Distributed PostgreSQL](https://www.yugabyte.com/postgresql/distributed-postgresql/)

Why you need Distributed PostgreSQL to truly scale.

##### Blog: [Data Replication in YugabyteDB](https://www.yugabyte.com/blog/data-replication/)

Learn the different data replication options available with YugabyteDB.

##### Documentation: [YugabyteDB architecture](preview/architecture/)

Learn about the internals of query, transactions, sharding, replication, and storage layers.

##### Blog: [Improving PostgreSQL: How to Overcome the Tough Challenges with YugabyteDB](https://www.yugabyte.com/blog/improve-postgresql/)

Problem areas in PostgreSQL and how to resolve them in YugabyteDB.

## Migrate from RBDMS

##### Playlist: [Database Migration using YugabyteDB Voyager](https://www.youtube.com/playlist?list=PL8Z3vt4qJTkJuqQ2ZH1cnL1yxVEi9swwR)

Learn how you can migrate databases to YugabyteDB quickly and securely.

##### Blog: [Simplify Database Migration with YugabyteDB Voyager](https://www.yugabyte.com/blog/simplify-database-migration-voyager/)

Use YugabyteDB Voyager to migrate from legacy and single-cloud databases.

##### Documentation: [YugabyteDB Voyager](preview/yugabyte-voyager/)

Simplify migration from legacy and cloud databases to YugabyteDB.

## Deployment options

##### Blog: [Multi-Region Best Practices](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/)

Techniques to reduce latencies and improve performance in a multi-region deployment.

##### Blog: [Engineering Around the Physics of Latency](https://www.yugabyte.com/blog/geo-distribution-in-yugabytedb-engineering-around-the-physics-of-latency/)

Learn about the geo-distributed deployment topologies offered by YugabyteDB.

##### Article: [Local Reads in Geo-Distributed YugabyteDB](https://dev.to/franckpachot/series/18625)

Perform local reads in geo-distributed YugabyteDB databases.

##### Blog: [Rapid Data Recovery](https://www.yugabyte.com/blog/rapid-data-recovery-database-amazon-s3/)

Learn how YugabyteDB performs swift data recovery.

##### Documentation: [Resiliency, high availability, and fault tolerance](preview/explore/fault-tolerance/)

Learn how YugabyteDB survives and recovers from outages.

##### Documentation: [Horizontal scalability](preview/explore/linear-scalability/)

Handle larger workloads by adding nodes to your cluster.

## Develop resilient applications

##### Documentation: [Hello world](preview/tutorials/build-apps/)

Use your favorite programming language to build a Hello world application.

##### Video: [Distributed PostgreSQL Essentials for Developers: Hands-on Course](https://www.youtube.com/watch?v=rqJBFQ-4Hgk)

Learn the essentials of building scalable and fault-tolerant applications.

##### Documentation: [Build global applications](preview/develop/build-global-apps/)

Learn how to design globally distributed applications using patterns.

##### Documentation: [Best practices](preview/develop/best-practices-ysql/)

Tips and tricks to build applications for high performance and availability.

##### Documentation: [Drivers and ORMs](preview/drivers-orms/)

Connect applications with your database.

##### Blog: [Database Connection Management: Exploring Pools and Performance](https://www.yugabyte.com/blog/database-connection-management/)

Database connection management with YugabyteDB.

##### Blog: [Understanding Client Connections in YugabyteDB YSQL](https://www.yugabyte.com/blog/how-connection-pooling-works/)

Understand client connections in YugabyteDB, and how connection pooling helps.
