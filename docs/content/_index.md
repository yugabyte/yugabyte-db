---
title: Documentation
headerTitle: Documentation Home
description: Guides, examples, and reference material you need to evaluate YugabyteDB database, build scalable applications, and learn distributed SQL.
headcontent: Evaluate YugabyteDB, build scalable applications, and learn distributed SQL
type: indexpage
layout: list
breadcrumbDisable: true
weight: 1
resourcesIntro: Explore based on your usecase
resources:
  - title: Multi-region deployments
    url: /preview/explore/multi-region-deployments/
  - title: Fully managed
    url: /preview/yugabyte-cloud/
  - title: Enterprise DBaaS
    url: /preview/yugabyte-platform/
  - title: Migrate from other RDBMS
    url: /preview/yugabyte-voyager/
---

YugabyteDB is distributed PostgreSQL that delivers on-demand scale, built-in resilience, flexible geo-distribution, and a multi-API (PostgreSQL-compatible and Cassandra-inspired) interface to power business-critical applications.

Using an innovative and re-architected distributed storage layer, YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures, and global data distribution. YugabyteDB can be deployed across public, private, and hybrid clouds as well as in Kubernetes environments.

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

### Explore

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="New to YugabyteDB"
    description="Why YugabyteDB should power your next cloud-native application."
    linkText1="Key benefits"
    linkUrl1="/preview/features/"
    linkText2="Quick start"
    linkUrl2="/preview/quick-start-yugabytedb-managed"
    linkText3="Explore YugabyteDB"
    linkUrl3="/preview/explore/"
  >}}

  {{< sections/3-box-card
    title="For Developers"
    description="Get instantly productive with familiar APIs and drivers."
    linkText1="Build a Hello world application"
    linkUrl1="/preview/develop/build-apps/"
    linkText2="Connect using drivers and ORMs"
    linkUrl2="/preview/drivers-orms/"
    linkText3="Distributed SQL APIs"
    linkUrl3="/preview/api/"
  >}}

  {{< sections/3-box-card
    title="For DevOps"
    description="Get flexible deployment and maintenance without downtime."
    linkText1="Hybrid and multi-cloud deployment"
    linkUrl1="/preview/develop/multi-cloud/"
    linkText2="Built-in resilience and availability"
    linkUrl2="/preview/explore/fault-tolerance/"
    linkText3="Effortless horizontal scaling"
    linkUrl3="/preview/explore/linear-scalability/"
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

<!--
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
-->

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

<!--
  {{< sections/3-box-card
    title="Distributed SQL APIs"
    linkText1="YSQL"
    linkUrl1="/preview/api/ysql/"
    linkText2="YCQL"
    linkUrl2="/preview/api/ycql/"
  >}}
-->
{{< /sections/3-boxes >}}
