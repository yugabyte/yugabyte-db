---
title: Product
linkTitle: Product
description: Product
aliases:
  - /faq/product/
menu:
  latest:
    identifier: product
    parent: faq
    weight: 2710
isTocNested: false
showAsideToc: true
---

## When is YugaByte DB a good fit?

YugaByte DB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability and low latency. Common use cases include:

1. Distributed Online Transaction Processing (OLTP) applications needing multi-region scalability without compromising strong consistency and low latency. E.g. User identity, Retail product catalog, Financial data service.

2. Hybrid Transactional/Analytical Processing (HTAP), also known as Translytical, applications needing real-time analytics on transactional data. E.g User personalization, fraud detection, machine learning.

3. Streaming applications needing to efficiently ingest, analyze and store ever-growing data. E.g. IoT sensor analytics, time series metrics, real time monitoring.

A few such use cases are detailed [here](https://www.yugabyte.com/).

## When is YugaByte DB not a good fit?

YugaByte DB is not a good fit for traditional Online Analytical Processing (OLAP) use cases that need complete ad-hoc analytics. Use an OLAP store such as [Druid](http://druid.io/druid.html) or a data warehouse such as [Snowflake](https://www.snowflake.net/).


## How many major releases YugaByte DB has had so far?

YugaByte DB has had 4 major releases.

- [v0.9 Beta](https://blog.yugabyte.com/yugabyte-has-arrived/) in November 2017
- [v1.0](https://blog.yugabyte.com/announcing-yugabyte-db-1-0-%F0%9F%8D%BE-%F0%9F%8E%89/) in May 2018
- [v1.1](https://blog.yugabyte.com/announcing-yugabyte-db-1-1-and-company-update/) in September 2018
- [v1.2](https://blog.yugabyte.com/announcing-yugabyte-db-1-2-company-update-jepsen-distributed-sql/) in March 2019

Next major release is the v2.0 release in Summer 2019.

## Can I deploy YugaByte DB to production?

Yes, YugaByte DB is [production ready](https://blog.yugabyte.com/yugabyte-db-1-0-a-peek-under-the-hood/) starting with v1.0 in May 2018. The YSQL API is in beta and is expected to reach production readiness in the v2.0 (Summer 2019).

## Which companies are currently using YugaByte DB in production?

Reference deployments are listed [here](https://www.yugabyte.com/all-resources/resource-parent/case-studies/).

## What is the definition of the "Beta" feature tag?

Some features are marked Beta in every release. Following are the points to consider:

- Code is well tested. Enabling the feature is considered safe. Some of these features enabled by default.

- Support for the overall feature will not be dropped, though details may change in incompatible ways in a subsequent beta or GA release. 

- Recommended only for non-production use.

Please do try our beta features and give feedback on them on our [Slack channel](https://www.yugabyte.com/slack) or by filing a [GitHub issue](https://github.com/YugaByte/yugabyte-db/issues).

## Any performance benchmarks available?

[Yahoo Cloud Serving Benchmark (YCSB)](https://github.com/brianfrankcooper/YCSB/wiki) is a popular benchmarking framework for NoSQL databases. We benchmarked YugaByte Cassandra API against the standard Apache Cassandra using YCSB. YugaByte DB outperformed Apache Cassandra by increasing margins as the number of keys (data density) increased across all the 6 YCSB workload configurations. 

[Netflix Data Benchmark (NDBench)](https://github.com/Netflix/ndbench) is another publicly available, cloud-enabled benchmark tool for data store systems. We ran NDBench against YugaByte DB for 7 days and observed P99 and P995 latencies that were orders of magnitude less than that of Apache Cassandra. 

Details for both the above benchhmarks are published in [Building a Strongly Consistent Cassandra with Better Performance](https://blog.yugabyte.com/building-a-strongly-consistent-cassandra-with-better-performance-aa96b1ab51d6).

## What about correctness testing?

[Jepsen](https://jepsen.io/) is a widely used framework to evaluate databases’ behavior under different failure scenarios. It allows for a database to be run across multiple nodes, and create artificial failure scenarios, as well as verify the correctness of the system under these scenarios. YugaByte DB 1.2 passes [formal Jepsen testing](https://blog.yugabyte.com/yugabyte-db-1-2-passes-jepsen-testing/). 

## Is the Community Edition open source?

Yes, the Community Edition is a completely open source, fully functioning version of YugaByte DB. It is licensed under Apache 2.0 and the source is available on [GitHub](https://github.com/yugabyte/yugabyte-db).

## How do the Community Edition and the Enterprise Edition differ from each other?

[Community Edition](../../quick-start/) is the best choice for the startup organizations with strong technical operations expertise looking to deploy YugaByte DB into production with traditional DevOps tools.

[Enterprise Edition](../../deploy/enterprise-edition/) includes all the features of the Community Edition as well as additional features such as built-in cloud native operations, enterprise-grade deployment options and world-class support. It is the simplest way to run YugaByte DB in mission-critical production environments with one or more regions (across both public cloud and on-premises datacenters).

A more detailed comparison of the two editions is available [here](https://www.yugabyte.com/community-edition/#compare-editions).

## How does YugaByte DB compare to other SQL and NoSQL databases?

See [YugaByte DB in Comparison](../../comparisons/)

- [Google Cloud Spanner](../../comparisons/google-spanner/)
- [CockroachDB](https://www.yugabyte.com/yugabyte-db-vs-cockroachdb/)
- [MongoDB](../../comparisons/mongodb/)
- [FoundationDB](../../comparisons/foundationdb/)
- [Amazon DynamoDB](../../comparisons/amazon-dynamodb/)
- [Apache Cassandra](../../comparisons/cassandra/)
- [Azure Cosmos DB](../../comparisons/azure-cosmos/)
- [Apache HBase](../../comparisons/hbase/)
- [Redis In-Memory Store](../../comparisons/redis/)

## What is the status of the YEDIS API?

In the near-term, YugaByte is not actively working on new feature or driver enhancements to the [YEDIS](../../yedis/) API other than bug fixes and stability improvements. Current focus is on [YSQL](../../api/ysql) and [YCQL](../../api/ycql).

For key-value workloads that need persistence, elasticity and fault-tolerance, YCQL (with notion of keyspaces, tables, role-based acces control and more) is often a great fit, especially if the application new rather than an existing one already written in Redis. The YCQL drivers are also more clustering aware, and hence YCQL is expected to perform better than YEDIS for equivalent scenarios. In general, our new feature development (support for data types, built-ins, TLS, backups and more), correctness testing (using Jepsen) and performance optimization is in the YSQL and YCQL areas.