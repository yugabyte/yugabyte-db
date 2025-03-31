---
title: YB Documentation
headerTitle: YugabyteDB Documentation
headcontent: Build scalable applications and learn distributed SQL
description: Guides, examples, and reference material you need to evaluate YugabyteDB database, build scalable applications, and learn distributed SQL.
type: indexpage
layout: list
breadcrumbDisable: true
weight: 1
showRightNav: true
unversioned: true
---

<!--YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a combination of high-performance document store, per-shard distributed consensus replication, and multi-shard ACID transactions, YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures, and global data distribution. As a cloud-native database, it can be deployed across public and private clouds as well as in Kubernetes environments.

YugabyteDB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency.-->

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
    buttonUrl="/preview/tutorials/quick-start/"
    imageAlt="Locally Laptop" imageUrl="/images/homepage/locally-laptop.svg"
  >}}
{{< /sections/2-boxes >}}

{{< sections/3-boxes-top-image >}}
  {{< sections/3-box-card
    title="Run a real world global app"
    description="Explore core features of YugabyteDB using a globally distributed application in real time."
    buttonText="Product Lab"
    buttonUrl="/preview/yugabyte-cloud/managed-labs/"
    imageAlt="Run a real world demo app"
    imageUrl="/images/homepage/run-real-world-demo-app.svg"
  >}}

  {{< sections/3-box-card
    title="Build a Hello World application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster."
    buttonText="Hello World"
    buttonUrl="/preview/tutorials/build-apps/"
    imageAlt="Build a Hello world application"
    imageUrl="/images/homepage/build-hello-world-application.svg"
  >}}

  {{< sections/3-box-card
    title="Learn the essentials"
    description="Learn YugabyteDB essential features by building a scalable and fault-tolerant streaming application."
    buttonText="Build and Learn"
    buttonUrl="/preview/tutorials/build-and-learn/"
    imageAlt="Explore Distributed SQL capabilities"
    imageUrl="/images/homepage/explore-distributed-sql-capabilities.svg"
  >}}
{{< /sections/3-boxes-top-image >}}

<!--
{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="New to YugabyteDB"
    linkText3="Explore YugabyteDB"
    linkUrl3="/preview/explore/"
  >}}

  {{< sections/3-box-card
    title="Develop"
    linkText1="Hello world"
    linkUrl1="/preview/tutorials/build-apps/"
    linkText2="Drivers and ORMs"
    linkUrl2="/preview/drivers-orms/"
    linkText3="Distributed SQL APIs"
    linkUrl3="/preview/api/"
  >}}

  {{< sections/3-box-card
    title="Why YugabyteDB"
    linkText2="Resilience and availability"
    linkUrl2="/preview/explore/fault-tolerance/"
    linkText3="Horizontal scaling"
    linkUrl3="/preview/explore/linear-scalability/"
    linkText1="Flexible deployment"
    linkUrl1="/preview/explore/multi-region-deployments/"
  >}}

{{< /sections/3-boxes >}}

### Products

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="YugabyteDB"
    description="The open source cloud-native distributed SQL database."
    buttonText="Explore YugabyteDB"
    buttonUrl="/preview/"
    imageAlt="YugabyteDB Core"
    imageUrl="/icons/database.svg"
  >}}

  {{< sections/3-box-card
    title="YugabyteDB Anywhere"
    description="Deploy YugabyteDB across any cloud and manage deployments via automation."
    buttonText="Documentation"
    buttonUrl="/preview/yugabyte-platform/"
    imageAlt="YugabyteDB Anywhere"
    imageUrl="/icons/server.svg"
  >}}

  {{< sections/3-box-card
    title="YugabyteDB Managed"
    description="Create and connect to a scalable, resilient, PostgreSQL-compatible database in minutes."
    buttonText="Documentation"
    buttonUrl="/preview/yugabyte-cloud/"
    imageAlt="YugabyteDB Managed"
    imageUrl="/icons/cloud.svg"
  >}}

  {{< sections/3-box-card
    title="YugabyteDB Voyager"
    description="Simplify migration from legacy and cloud databases to YugabyteDB."
    buttonText="Documentation"
    buttonUrl="/preview/yugabyte-voyager/"
    imageAlt="YugabyteDB Managed"
    imageUrl="/images/migrate/migration-icon.svg"
  >}}
{{< /sections/3-boxes >}}

### New to YugabyteDB?

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Key benefits"
    description="What is YugabyteDB, and why it should power your cloud-native applications."
    buttonText="Key benefits"
    buttonUrl="/preview/features/"
  >}}

  {{< sections/3-box-card
    title="Quick start"
    description="Create your first cluster, explore distributed SQL, and build a sample application in 15 minutes."
    buttonText="Get Started"
    buttonUrl="/preview/quick-start-yugabytedb-managed/"
  >}}

  {{< sections/3-box-card
    title="Explore YugabyteDB"
    description="Test YugabyteDB's features through examples, including API compatibility, availability, scalability, and more."
    buttonText="Explore"
    buttonUrl="/preview/explore/"
  >}}
{{< /sections/3-boxes >}}

### For developers

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Build a Hello world application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster."
    buttonText="Build a Hello world application"
    buttonUrl="/preview/develop/build-apps/"
  >}}

  {{< sections/3-box-card
    title="Connect using drivers and ORMs"
    description="Connect applications using familiar third-party divers and ORMs and YugabyteDB Smart Drivers."
    buttonText="Drivers and ORMs"
    buttonUrl="/preview/drivers-orms/"
  >}}

  {{< sections/3-box-card
    title="Common patterns"
    description="Leverage common data models to design robust and efficient cloud-native applications."
    buttonText="Common patterns"
    buttonUrl="/preview/develop/common-patterns//"
  >}}
{{< /sections/3-boxes >}}

### Operations

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Deploy YugabyteDB"
    description="Deploy YugabyteDB to the public cloud, a private data center, or Kubernetes."
    buttonText="Deploy YugabyteDB"
    buttonUrl="/preview/deploy/"
  >}}

  {{< sections/3-box-card
    title="Manage your deployment"
    description="Back up, change cluster configuration, and upgrade deplopyments."
    buttonText="Manage your deployment"
    buttonUrl="/preview/manage/"
  >}}

  {{< sections/3-box-card
    title="Migrate"
    description="Use YugabyteDB Voyager to manage end-to-end database migration."
    buttonText="Migrate"
    buttonUrl="/preview/yugabyte-voyager/"
  >}}
{{< /sections/3-boxes >}}

## Learn through examples

{{< sections/3-boxes-top-image >}}
  {{< sections/3-box-card
    title="Build a Hello world application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster."
    buttonText="Get started"
    buttonUrl="/preview/develop/build-apps/"
    imageAlt="Build a Hello world application"
    imageUrl="/images/homepage/build-hello-world-application.svg"
  >}}

  {{< sections/3-box-card
    title="Run a real world demo app"
    description="Run a distributed full-stack e-commerce application built on YugabyteDB, Node.js Express, and React."
    buttonText="Get started"
    buttonUrl="/preview/develop/realworld-apps/ecommerce-app/"
    imageAlt="Run a real world demo app"
    imageUrl="/images/homepage/run-real-world-demo-app.svg"
  >}}

  {{< sections/3-box-card
    title="Explore Distributed SQL capabilities"
    description="Test YugabyteDB's compatibility with standard PostgreSQL features, such as data types, queries, expressions, and more."
    buttonText="Get started"
    buttonUrl="/preview/explore/"
    imageAlt="Explore Distributed SQL capabilities"
    imageUrl="/images/homepage/explore-distributed-sql-capabilities.svg"
  >}}
{{< /sections/3-boxes-top-image >}}

## Key concepts

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Understand availability"
    description="Find out how a YugabyteDB cluster continues to do reads and writes when a node fails."
    buttonText="Read real world availability usecases"
    buttonUrl="/preview/explore/fault-tolerance/macos/"
    imageAlt="Understand availability"
    imageUrl="/icons/availability.svg"
  >}}

  {{< sections/3-box-card
    title="Understand scalability"
    description="Scale a cluster and see how YugabyteDB dynamically distributes transactions."
    buttonText="Read real world scalability usecases"
    buttonUrl="/preview/explore/linear-scalability/"
    imageAlt="Understand scalability"
    imageUrl="/icons/scalability.svg"
  >}}

  {{< sections/3-box-card
    title="Understand geo-partitioning"
    description="See how moving data closer to users can reduce latency and improve performance."
    buttonText="Read real world Geo usecases"
    buttonUrl="/preview/explore/multi-region-deployments/row-level-geo-partitioning/"
    imageAlt="Understand Geo"
    imageUrl="/icons/geo.svg"
  >}}
{{< /sections/3-boxes >}}

### More resources

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Yugabyte University"
    description="Learn YugabyteDB via self-paced courses, virtual training, and builder workshops."
    buttonText="Learn more"
    buttonUrl="https://university.yugabyte.com/"
  >}}

  {{< sections/3-box-card
    title="Get Under the Hood"
    linkText1="YugabyteDB architecture"
    linkUrl1="/preview/architecture/"
    linkText2="Benchmark YugabyteDB"
    linkUrl2="/preview/benchmark/"
  >}}

  {{< sections/3-box-card
    title="More Links"
    linkText1="Download"
    linkUrl1="https://download.yugabyte.com"
    linkText3="YugabyteDB Blog"
    linkUrl3="https://www.yugabyte.com/blog/"
  >}}

  {{< sections/3-box-card
    title="Distributed SQL APIs"
    linkText1="YSQL"
    linkUrl1="/preview/api/ysql/"
    linkText2="YCQL"
    linkUrl2="/preview/api/ycql/"
  >}}
{{< /sections/3-boxes >}}

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

## Migrate from RDBMS

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
-->
