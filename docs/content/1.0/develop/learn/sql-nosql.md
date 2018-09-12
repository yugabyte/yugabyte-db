---
title: 1. SQL vs NoSQL
linkTitle: 1. SQL vs NoSQL
description: SQL vs NoSQL
aliases:
  - /develop/learn/sql-nosql/
menu:
  1.0:
    identifier: sql-nosql
    parent: learn
    weight: 561
---

Most application developers have used SQL and possibly some NoSQL databases to build applications. YugaByte DB brings the best of these two databases together into one unified platform to simplify development of scalable cloud services.

Very often, today's cloud services and applications will start out with just a few requests and a very small amount of data. These can be served by just a few nodes. But if the application becomes popular, they would have to scale out rapidly in order to handle millions of requests and many terabytes of data. YugaByte DB is well suited for these kinds of workloads.

## Unifying SQL and NoSQL

Here are a few different criteria where YugaByte DB brings the best of SQL and NoSQL together into a single database platform.

### Data characteristics

These can be loosely defined as the high-level concerns when choosing a database to build an application or a cloud service - such as its data model, the API it supports, its consistency semantics and so on. Here is a table that contrasts what YugaByte DB offers with SQL and NoSQL databases in general. Note that there are a number of different NoSQL databases each with their own nuanced behavior, and the table below is not accurate for all NoSQL databases - it is just meant to give an idea.

| Database Characteristics  | SQL | NoSQL | YugaByte DB |
| --------------- | ---------------- | ------------------ | ------------------ |
| Data model | Well-defined schema (tables, rows, columns)  | Schemaless | Both |
| API    | SQL | Various | Goal -  PostgreSQL + Cassandra + Redis |
| Consistency | Strong consistency | Eventual consistency | Strong consistency |
| Transactions | ACID transactions | No transactions | ACID transactions |
| High Write Throughput | No | Sometimes | Yes
| Tunable read latency | No | Yes | Yes


### Operational characteristics

Operational characteristics can be defined as the runtime concerns that arise when a database is deployed, run and managed in production. When running a database in production in a cloud-like architecture, there are a number of operational characteristics that become essential. Operationally here are the capabilities of YugaByte DB compared to SQL and NoSQL databases. As before, there are a number of NoSQL databases which are different in their own ways and the table below is meant to give a broad idea.

| Operational Characteristics  | SQL | NoSQL | YugaByte DB |
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
| Read replica support | No - manual setup | No - no async replication | Yes - sync and async replication options


## Core features

Applications and cloud services depend on databases for a variety of built-in features. These can include the ability to perform multi-row transactions, JSON or document support, secondary indexes, automatic data expiry with TTLs, and so on.

Here is a table that lists some of the important features that YugaByte DB supports, and which of YugaByte DB's APIs to use in order to achieve these features. Note that typically, multiple databases are deployed in order to achieve these features.

| Database Features  | YugaByte DB - Cassandra-compatible YCQL API | YugaByte DB - Redis-compatible YEDIS API|
| --------------- | ---------------- | ------------------ |
| Multi-row transactions | Yes | - |
| Consistent secondary indexes | Coming soon | - |
| JSON/document support | Roadmap - JSON datatype coming soon | Yes - supports primitive types, maps, lists, (sorted) sets |
| Secondary Indexes | Coming soon | - |
| High Throughput | Yes - batch inserts | Yes - pipelined operations |
| Automatic data expiry with TTL | Yes - table and column level TTL | Yes - key level TTL |
| Run Apache Spark for AI/ML | Yes | - |


## Linear scalability

In order to test the linear scalability of YugaByte DB, we have run some large cluster benchmarks (upto 50 nodes). We were able to scale YugaByte DB to million of reads and writes per second while retaining low latencies. You can read more about our [large cluster tests and how we scaled YugaByte DB to millions of IOPS](https://blog.yugabyte.com/scaling-yugabyte-db-to-millions-of-reads-and-writes-fb86cea5ff15).

![Linear Scalability at large cluster sizes](/images/develop/learn/yb-scale-out.png)

## High performance

YugaByte DB was built with a performance as a design goal. Performance in a public cloud environment without sacrificing consistency is a serious ask. YugaByte DB has been written ground up in C++ for this very reason. Here is a chart showing how YugaByte DB compares with Apache Cassandra when running a YCSB benchmark. Read more about the [YCSB benchmark results and what makes YugaByte DB performant](https://blog.yugabyte.com/building-a-strongly-consistent-cassandra-with-better-performance-aa96b1ab51d6).

The first chart below shows the total ops/second when running YBSB benchmark.

![YCSB Benchmark - ops/sec](/images/develop/learn/yb-perf-ycsb-ops.png)

The second chart below shows the latency for the YCSB run.

![YCSB Benchmark - latency](/images/develop/learn/yb-perf-ycsb-latency.png)


## Geo-distributed

This is a screenshot of YugaByte DB EE, which visualized the universe created. Below is a screenshot of a 5-node YugaByte DB universe created for a user identity use-case to power users logging in and changing passwords for a SaaS application. The replication factor of this universe is 5, and it is configured to keep 2 copies of data in us-west, 2 copies of the data in us-east and 1 copy of the data in asia-pacific region. 

![Geo-distributed](/images/develop/learn/yb-geo-distributed.png)

Because of this configuration, this universe can:

- Allow low read latencies from any region (follower reads from a nearby datacenter)
- Allow strongly consistent, global writes
- Survive the outage of any region

![Geo-distributed ops/sec](/images/develop/learn/yb-geo-distributed-ops.png)
![Geo-distributed latency](/images/develop/learn/yb-geo-distributed-latency.png)

The graphs above, also taken from the EE, show that the average read latencies for apps running the the various cloud regions are just 250 microseconds, while writes are strongly consistent and incur 218 milliseconds.


## Multi-cloud ready

It is possible to easily configure YugaByte DB EE to work with multiple public clouds as well as private datacenters in just a few minutes.

![Geo-distributed](/images/develop/learn/yb-multi-cloud-ready.png)

