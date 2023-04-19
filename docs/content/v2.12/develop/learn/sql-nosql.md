---
title: SQL vs NoSQL in YugabyteDB
headerTitle: SQL vs NoSQL
linkTitle: 1. SQL vs NoSQL
description: Learn how YugabyteDB brings the best of SQL and NoSQL together into one unified platform to simplify development of scalable cloud services.
menu:
  v2.12:
    identifier: sql-nosql
    parent: learn
    weight: 561
type: docs
---

Most application developers have used SQL, and possibly some NoSQL databases, to build applications. YugabyteDB brings the best of these two databases together into one unified platform to simplify development of scalable cloud services.

Very often, today's cloud services and applications will start out with just a few requests and a very small amount of data. These can be served by just a few nodes. But if the application becomes popular, they would have to scale out rapidly in order to handle millions of requests and many terabytes of data. YugabyteDB is well suited for these kinds of workloads.

## Unifying SQL and NoSQL

Here are a few different criteria where YugabyteDB brings the best of SQL and NoSQL together into a single database platform.

### Database characteristics

These can be loosely defined as the high-level concerns when choosing a database to build an application or a cloud service - such as its data model, the API it supports, its consistency semantics, and so on. The following table contrasts what YugabyteDB offers with SQL and NoSQL databases in general. Note that there are a number of different NoSQL databases each with their own nuanced behavior, and the table is not accurate for all NoSQL databases - it is just meant to give an idea.

| Characteristic | SQL | NoSQL | YugabyteDB |
| --------------- | ---------------- | ------------------ | ------------------ |
| Data model | Well-defined schema (tables, rows, columns)  | Schema-less | Both |
| API    | SQL | Various | Fully-relational SQL +<br/> Semi-relational SQL |
| Consistency | Strong | Eventual | Strong |
| Transactions | ACID transactions | None | ACID transactions |
| High write throughput | No | Sometimes | Yes
| Tunable read latency | No | Yes | Yes

### Operational characteristics

Operational characteristics can be defined as the runtime concerns that arise when a database is deployed, run, and managed in production. When running a database in production in a cloud-like architecture, a number of operational characteristics become essential. The following table compares the capabilities of YugabyteDB to SQL and NoSQL databases. As with the preceding section, there are a number of NoSQL databases which are different in their own ways and the following table is meant to give a broad idea.

The following table lists some of the important features that YugabyteDB supports, and the API to use to leverage the feature. Note that typically, multiple databases are deployed to achieve the same functionality.

| Operational characteristics  | SQL | NoSQL | YugabyteDB |
| --------------- | ---------------- | ------------------ | ------------------ |
| Automatic sharding | No | Sometimes | Yes
| Linear scalability | No | Yes | Yes
| Fault tolerance | No - manual setup | Yes - smart client detects failed nodes | Yes - smart client detects failed nodes
| Data resilience | No | Yes - but rebuilds cause high latencies | Yes - automatic, efficient data rebuilds
| Geo-distributed | No - manual setup | Sometimes | Yes
| Low latency reads | No | Yes | Yes
| Predictable p99 read latency | Yes | No | Yes
| High data density | No | Sometimes - latencies suffer when densities increase | Yes - predictable latencies at high data densities
| Tunable reads with timeline consistency | No - manual setup | Sometimes | Yes
| Read replica support | No - manual setup | No - no asynchronous replication | Yes - synchronous and asynchronous replication options

## Core features

Applications and cloud services depend on databases for a variety of built-in features. These can include the ability to perform multi-row transactions, JSON or document support, secondary indexes, automatic data expiration with TTLs, and so on.

Here is a table that lists some of the important features that YugabyteDB supports, and which of YugabyteDB's APIs to use in order to achieve these features. Note that typically, multiple databases are deployed in order to achieve these features.

| Database features  | [Yugabyte SQL API](../../../api/ysql) | [Yugabyte Cloud QL API](../../../api/ycql) |
| --------------- | ---------------- | ------------------ |
| Multi-row transactions | Yes | Yes |
| Consistent secondary indexes | Yes | Yes |
| JSON/document support | Yes | Yes |
| Secondary Indexes | Yes | Yes |
| Foreign keys | Yes | No |
| JOINs | Yes | No |
| Automatic data expiry with TTL | No | Yes - table and column level TTL |
| Run Apache Spark for AI/ML | No | Yes |

## Linear scalability

In order to test the linear scalability of YugabyteDB, we have run some large cluster benchmarks (up to 50 nodes). We were able to scale YugabyteDB to million of reads and writes per second while retaining low latencies. You can read more about our [large cluster tests and how we scaled YugabyteDB to millions of IOPS](https://www.yugabyte.com/blog/building-a-strongly-consistent-cassandra-with-better-performance/).

![Linear scalability at large cluster sizes](/images/develop/learn/yb-scale-out.png)

## High performance

YugabyteDB was built to provide performance in a public cloud environment without sacrificing consistency. YugabyteDB is written in C++ for this very reason. The following chart shows how YugabyteDB compares with Apache Cassandra when running a YCSB benchmark. Read more about the [YCSB benchmark results and what makes YugabyteDB performant](https://www.yugabyte.com/blog/scaling-yugabyte-db-to-millions-of-reads-and-writes/).

The following chart shows the total ops/second when running YBSB benchmark:

![YCSB Benchmark - ops/sec](/images/develop/learn/yb-perf-ycsb-ops.png)

The following chart shows the latency for the YCSB run:

![YCSB Benchmark - latency](/images/develop/learn/yb-perf-ycsb-latency.png)

## Geo-distributed

The following illustration shows a 5-node YugabyteDB universe created for a user identity use-case to power users logging in and changing passwords for a SaaS application. The replication factor of this universe is 5, and it is configured to keep 2 copies of data in `us-west`, 2 copies of the data in `us-east`, and 1 copy of the data in the Asia-Pacific region.

![Geo-distributed](/images/develop/learn/yb-geo-distributed.png)

Because of this configuration, this universe can:

- Allow low read latencies from any region (follower reads from a nearby data center)
- Allow strongly consistent, global writes
- Survive the outage of any region

![Geo-distributed ops/sec](/images/develop/learn/yb-geo-distributed-ops.png)
![Geo-distributed latency](/images/develop/learn/yb-geo-distributed-latency.png)

The preceding graphs, also taken from YugabyteDB Anywhere, show that the average read latencies for apps running the various cloud regions are just 250 microseconds, while writes are strongly consistent, and incur 218 milliseconds.

## Multi-cloud ready

You can configure YugabyteDB Anywhere to work with multiple public clouds as well as private data centers in just a few minutes.

![Geo-distributed](/images/develop/learn/yb-multi-cloud-ready.png)
