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
    weight: 2470
---

## When is YugaByte DB a good fit?

YugaByte DB is a good fit for cloud-native applications needing to serve mission-critical data reliably, with zero data loss, high availability and low latency. Common use cases include:

1. Online Transaction Processing (OLTP) applications needing multi-region availability without compromising strong consistency and . E.g. User identity, retail product catalog, financial data service.

2. Hybrid Transactional/Analytical Processing (HTAP), also known as Translytical, applications needing real-time analytics on transactional data. E.g User personalization, fraud detection, machine learning.

3. Streaming applications needing to efficiently ingest, analyze and store ever-growing data. E.g. IoT sensor analytics, time series metrics, real time monitoring.

A few such use cases are detailed [here](https://www.yugabyte.com/solutions/).

## When is YugaByte DB not a good fit?

YugaByte DB is not a good fit for traditional Online Analytical Processing (OLAP) use cases that need complete ad-hoc analytics. Use an OLAP store such as [Druid](http://druid.io/druid.html) or a data warehouse such as [Snowflake](https://www.snowflake.net/).

## Can I deploy YugaByte DB to production?

Yes, YugaByte DB is production-ready starting with the 1.0 release in May 2018.

## What is the definition of the "Beta" feature tag?

Some features are marked Beta in every release. Following are the points to consider:

- Code is well tested. Enabling the feature is considered safe. Enabled by default.

- Support for the overall feature will not be dropped, though details may change in incompatible ways in a subsequent beta or GA release. 

- Recommended only for non-production use.

Please do try our beta features and give feedback on them.

## Any performance benchmarks available?

[Yahoo Cloud Serving Benchmark (YCSB)](https://github.com/brianfrankcooper/YCSB/wiki) is a popular benchmarking framework for NoSQL databases. We benchmarked YugaByte Cassandra API against the standard Apache Cassandra using YCSB. YugaByte outperformed Apache Cassandra by increasing margins as the number of keys (data density) increased across all the 6 YCSB workload configurations. 

[Netflix Data Benchmark (NDBench)](https://github.com/Netflix/ndbench) is another publicly available, cloud-enabled benchmark tool for data store systems. We ran NDBench against YugaByte DB for 7 days and observed P99 and P995 latencies that were orders of magnitude less than that of Apache Cassandra. 

Details for both the above benchhmarks are published in [Building a Strongly Consistent Cassandra with Better Performance](https://blog.yugabyte.com/building-a-strongly-consistent-cassandra-with-better-performance-aa96b1ab51d6).

## What about correctness testing?

[Jepsen](https://jepsen.io/) is a widely used framework to evaluate databases’ behavior under different failure scenarios. It allows for a database to be run across multiple nodes, and create artificial failure scenarios, as well as verify the correctness of the system under these scenarios. We have started testing YugaByte DB with Jepsen and will post the results on the [YugaByte Forum](https://forum.yugabyte.com/t/validating-yugabyte-db-with-jepsen/73) as they become available.

## Is the Community Edition open source?

Yes, the Community Edition is a completely open source, fully functioning version of YugaByte DB. It is licensed under Apache 2.0 and the source is available on [GitHub](https://github.com/yugabyte/yugabyte-db).

## How do the Community Edition and the Enterprise Edition differ from each other?

[Community Edition](../../quick-start/) is the best choice for the startup organizations with strong technical operations expertise looking to deploy YugaByte DB into production with traditional DevOps tools. 

[Enterprise Edition](../../deploy/enterprise-edition/) includes all the features of the Community Edition as well as additional features such as built-in cloud-native operations, enterprise-grade deployment options and world-class support. It is the simplest way to run YugaByte DB in mission-critical production environments with one or more regions (across both public cloud and on-premises datacenters).

A more detailed comparison of the two editions is available [here](https://www.yugabyte.com/product/compare/).

## How does YugaByte DB compare to other SQL and NoSQL databases?

See [YugaByte DB in Comparison](../../comparisons/)

- [Apache Cassandra](../../comparisons/cassandra/)
- [MongoDB](../../comparisons/mongodb/)
- [Redis](../../comparisons/redis/)
- [Apache HBase](../../comparisons/hbase/)
- [Google Cloud Spanner](../../comparisons/google-spanner/)
- [Azure Cosmos DB](../../comparisons/azure-cosmos/)

## Why not use a Redis cluster alongside a sharded SQL cluster?

Independent cache and database clusters should be avoided for multiple reasons.

### Development and operational complexity

Sharding has to be implemented at two tiers. Scale out (expanding/shrinking the cluster) has to be re-implemented twice - once at the Redis layer and again for the sharded SQL layer. You need to also worry about resharding the data in the Redis layer. The application has to be aware of two tiers, move data between them, deal with consistency issues, etc. It has to write to Redis and sharded SQL, read from Redis, deal with staleness of data, etc.

### Higher cost

Typically not all the data needs to be cached, and the cache has to adapt to the query pattern. Most people deploying Redis this way end up caching all the data. Some queries need to be answered with low latency from the cache, and others are longer running queries that should not pollute the cache. This is hard to achieve if two systems deal with different access patterns for the same data.

### Cross-DC administration complexity

You need to solve all the failover and failback issues at two layers especially when both sync and async replicas are needed. Additionally, the latency/consistency/throughput tracking needs to be done at two levels, which makes the app deployment architecture very complicated.
