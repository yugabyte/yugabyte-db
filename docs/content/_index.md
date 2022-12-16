---
title: Documentation
headerTitle: Documentation
description: Find the guides, examples, and reference material you need to evaluate YugabyteDB, build scalable applications, and learn distributed SQL.
headcontent: Find the guides, examples, and reference material you need to evaluate YugabyteDB, build scalable applications, and learn distributed SQL.
type: indexpage
layout: list
breadcrumbDisable: true
weight: 1
resourcesIntro: Explore resources based on your usecase
resources:
  - title: Multi-region deployments
    url: /preview/explore/multi-region-deployments/
  - title: Fully managed
    url: /preview/yugabyte-cloud/
  - title: Enterprise DBaaS
    url: /preview/yugabyte-platform/
  - title: Migrate from other RDBMS
    url: /preview/migrate/
---

{{< sections/text-with-right-image
  title="Learn through examples"
  description="Microservices need a cloud native relational database that is resilient, scalable, and geo-distributed. YugabyteDB powers your modern applications"
  buttonText="Get started"
  buttonUrl="/preview/quick-start-yugabytedb-managed/"
  imageAlt="Yugabyte cloud" imageUrl="/images/homepage/learn-through-examples.svg"
>}}

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started in the Cloud"
    description="Create your first cluster, explore distributed SQL, and build a sample application in 15 minutes. No credit card required."
    buttonText="Get started"
    buttonUrl="/preview/quick-start-yugabytedb-managed/"
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

## Learn through examples

{{< sections/3-boxes-top-image >}}
  {{< sections/3-box-card
    title="Build a Hello world application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster and performs basic SQL operations."
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
    description="Learn about YugabyteDB's PostgreSQL compatibility."
    buttonText="Get started"
    buttonUrl="/preview/explore/"
    imageAlt="Explore Distributed SQL capabilities"
    imageUrl="/images/homepage/explore-distributed-sql-capabilities.svg"
  >}}
{{< /sections/3-boxes-top-image >}}

## Products

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="YugabyteDB"
    description="Open source cloud-native distributed SQL database for mission-critical applications."
    buttonText="Explore YugabyteDB"
    buttonUrl="/preview/explore/"
    imageAlt="Explore YugabyteDB"
    imageUrl="/icons/database.svg"
  >}}

  {{< sections/3-box-card
    title="YugabyteDB Anywhere"
    description="YugabyteDB delivered as a private database-as-a-service for enterprises."
    buttonText="Documentation"
    buttonUrl="/preview/yugabyte-platform/"
    imageAlt="YugabyteDB Anywhere"
    imageUrl="/icons/server.svg"
  >}}

  {{< sections/3-box-card
    title="YugabyteDB Managed"
    description="Fully managed YugabyteDB-as-a-Service without the operational overhead of managing a database."
    buttonText="Documentation"
    buttonUrl="/preview/yugabyte-cloud/"
    imageAlt="YugabyteDB Managed"
    imageUrl="/icons/cloud.svg"
  >}}
{{< /sections/3-boxes >}}

## Key concepts

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Understand availability"
    description="YugabyteDB clusters can continue to do reads and writes even in case of node failures."
    buttonText="Read real world availability usecases"
    buttonUrl="/preview/explore/fault-tolerance/macos/"
    imageAlt="Understand availability"
    imageUrl="/icons/availability.svg"
  >}}

  {{< sections/3-box-card
    title="Understand scalability"
    description="Scale YugabyteDB clusters to handle more transactions per second, more concurrent client connections, and larger datasets."
    buttonText="Read real world scalability usecases"
    buttonUrl="/preview/explore/linear-scalability/"
    imageAlt="Understand scalability"
    imageUrl="/icons/scalability.svg"
  >}}

  {{< sections/3-box-card
    title="Understand geo-partitioning"
    description="Use geo-partitioning to meet data residency requirements and achieve lower latency and higher performance."
    buttonText="Read real world Geo usecases"
    buttonUrl="/preview/explore/multi-region-deployments/row-level-geo-partitioning/"
    imageAlt="Understand Geo"
    imageUrl="/icons/geo.svg"
  >}}
{{< /sections/3-boxes >}}

## More resources

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Explore Yugabyte University"
    linkText1="Read real world availability usecases"
    linkUrl1="https://university.yugabyte.com/courses/introduction-to-distributed-sql"
    linkText2="Introduction to YugabyteDB"
    linkUrl2="https://university.yugabyte.com/courses/introduction-to-yugabytedb"
    linkText3="YugabyteDB Managed Basics"
    linkUrl3="https://university.yugabyte.com/courses/yugabytedb-managed-basics"
  >}}

  {{< sections/3-box-card
    title="Get under the hood"
    linkText1="YugabyteDB architecture"
    linkUrl1="/preview/architecture/"
    linkText2="Benchmark YugabyteDB"
    linkUrl2="/preview/benchmark/"
    linkText3="Drivers and ORMs"
    linkUrl3="/preview/drivers-orms/"
  >}}

  {{< sections/3-box-card
    title="Distributed SQL APIs"
    linkText1="YSQL"
    linkUrl1="/preview/api/ysql/"
    linkText2="YCQL"
    linkUrl2="/preview/api/ycql/"
  >}}
{{< /sections/3-boxes >}}
