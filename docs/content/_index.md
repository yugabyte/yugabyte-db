---
title: YB Documentation
headerTitle: YugabyteDB Documentation
headcontent: Distributed PostgreSQL for Modern Apps
description: Guides, examples, and reference material you need to evaluate YugabyteDB database, build scalable applications, and learn distributed SQL.
type: indexpage
layout: list
breadcrumbDisable: true
weight: 1
unversioned: true
---

YugabyteDB is a cloud-native distributed PostgreSQL-compatible database that combines strong consistency with ultra-resilience, seamless scalability, geo-distribution, and highly flexible data locality to deliver business-critical, transactional applications.

Available as a flexible service and with a uniform experience across Kubernetes and/or any combination of public, private, or hybrid cloud, YugabyteDB provides distributed PostgreSQL that scales and never fails.

YugabyteDB provides PostgreSQL without limits and is an excellent fit for new or modernized transactional applications that need absolute data correctness and require scalability, high availability, or global deployment.
<!--YugabyteDB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency.-->

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started"
    description="Create your first cluster and build a sample application in 15 minutes."
    buttonText="Get started"
    buttonUrl="/stable/quick-start-yugabytedb-managed/"
    imageAlt="Laptop" imageUrl="/images/homepage/locally-laptop.svg"
  >}}

  {{< sections/bottom-image-box
    title="Modernize and Migrate"
    description="Streamline all stages of bringing a source to YugabyteDB, including analysis, conversion, migration, and cutover."
    buttonText="Get started"
    buttonUrl="/stable/yugabyte-voyager/introduction/"
    imageAlt="Cloud" imageUrl="/images/homepage/yugabyte-in-cloud.svg"
  >}}
{{< /sections/2-boxes >}}

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Explore"
    description="Explore YugabyteDB's support for cloud-native applications."
    linkText1="PostgreSQL compatibility"
    linkUrl1="/stable/explore/ysql-language-features/"
    linkText2="Resilience"
    linkUrl2="/stable/explore/fault-tolerance/"
    linkText3="Scalability"
    linkUrl3="/stable/explore/linear-scalability/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="/stable/explore/"
  >}}
  {{< sections/3-box-card
    title="AI"
    description="Add a scalable and highly-available database to your AI projects."
    linkText1="RAG"
    linkUrl1="/stable/develop/ai/hello-rag/"
    linkText2="YugabyteDB MCP Server"
    linkUrl2="/stable/develop/ai/mcp-server/"
    linkText3="Vector AI"
    linkUrl3="/stable/develop/ai/ai-ollama/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="/stable/develop/ai/"
  >}}
  {{< sections/3-box-card
    title="Under the hood"
    description="Learn about YugabyteDB's modern architecture."
    linkText1="Query layer"
    linkUrl1="/stable/architecture/query-layer/"
    linkText2="Storage layer"
    linkUrl2="/stable/architecture/docdb/"
    linkText3="Transactions"
    linkUrl3="/stable/architecture/transactions/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="/stable/architecture/"
  >}}
{{< /sections/3-boxes >}}

## Develop for YugabyteDB

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="Build a Hello World application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster."
    buttonText="Build"
    buttonUrl="/stable/develop/tutorials/build-apps/"
  >}}

  {{< sections/3-box-card
    title="Connect using drivers and ORMs"
    description="Connect applications to your database using familiar third-party divers and ORMs and YugabyteDB Smart Drivers."
    buttonText="Connect"
    buttonUrl="/stable/develop/drivers-orms/"
  >}}

  {{< sections/3-box-card
    title="Use familiar APIs"
    description="Get up to speed quickly using YugabyteDB's PostgreSQL-compatible YSQL and Cassandra-based YCQL APIs."
    buttonText="Develop"
    buttonUrl="/stable/api/"
  >}}

{{< /sections/3-boxes >}}

<!--

  {{< sections/3-box-card
    title="Develop"
    description="Build global applications using familiar APIs and drivers."
    linkText1="Global applications"
    linkUrl1="/stable/develop/build-global-apps/"
    linkText2="Hybrid and multi-cloud"
    linkUrl2="/stable/develop/multi-cloud/"
    linkText3="Drivers and ORMs"
    linkUrl3="/stable/develop/drivers-orms/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="/stable/develop/"
  >}}

## Introduction to YugabyteDB

##### Video: [Introducing YugabyteDB: The Distributed SQL Database for Mission-Critical Applications](https://www.youtube.com/watch?v=j24p07Frw00)

Learn about YugabyteDB and how it supports mission-critical applications.

##### Article: [How to Scale a Single-Server Database: A Guide to Distributed PostgreSQL](https://www.yugabyte.com/postgresql/distributed-postgresql/)

Why you need Distributed PostgreSQL to truly scale.

##### Blog: [Data Replication in YugabyteDB](https://www.yugabyte.com/blog/data-replication/)

Learn the different data replication options available with YugabyteDB.

##### Documentation: [YugabyteDB architecture](stable/architecture/)

Learn about the internals of query, transactions, sharding, replication, and storage layers.

##### Blog: [Improving PostgreSQL: How to Overcome the Tough Challenges with YugabyteDB](https://www.yugabyte.com/blog/improve-postgresql/)

Problem areas in PostgreSQL and how to resolve them in YugabyteDB.

## Migrate from RDBMS

##### Playlist: [Database Migration using YugabyteDB Voyager](https://www.youtube.com/playlist?list=PL8Z3vt4qJTkJuqQ2ZH1cnL1yxVEi9swwR)

Learn how you can migrate databases to YugabyteDB quickly and securely.

##### Blog: [Simplify Database Migration with YugabyteDB Voyager](https://www.yugabyte.com/blog/simplify-database-migration-voyager/)

Use YugabyteDB Voyager to migrate from legacy and single-cloud databases.

##### Documentation: [YugabyteDB Voyager](stable/yugabyte-voyager/)

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

##### Documentation: [Resiliency, high availability, and fault tolerance](stable/explore/fault-tolerance/)

Learn how YugabyteDB survives and recovers from outages.

##### Documentation: [Horizontal scalability](stable/explore/linear-scalability/)

Handle larger workloads by adding nodes to your cluster.

## Develop resilient applications

##### Documentation: [Hello world](stable/develop/tutorials/build-apps/)

Use your favorite programming language to build a Hello world application.

##### Video: [Distributed PostgreSQL Essentials for Developers: Hands-on Course](https://www.youtube.com/watch?v=rqJBFQ-4Hgk)

Learn the essentials of building scalable and fault-tolerant applications.

##### Documentation: [Build global applications](stable/develop/build-global-apps/)

Learn how to design globally distributed applications using patterns.

##### Documentation: [Best practices](stable/develop/best-practices-ysql/)

Tips and tricks to build applications for high performance and availability.

##### Documentation: [Drivers and ORMs](stable/develop/drivers-orms/)

Connect applications with your database.

##### Blog: [Database Connection Management: Exploring Pools and Performance](https://www.yugabyte.com/blog/database-connection-management/)

Database connection management with YugabyteDB.

##### Blog: [Understanding Client Connections in YugabyteDB YSQL](https://www.yugabyte.com/blog/how-connection-pooling-works/)

Understand client connections in YugabyteDB, and how connection pooling helps.
-->
