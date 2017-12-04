---
title: Frequently Asked Questions
weight: 2460
---

## Product

### When is YugaByte DB a good fit?

YugaByte DB is a good fit for cloud-native applications needing to serve mission-critical data reliably, with zero data loss, high availability and low latency. Common use cases include:

1. Online Transaction Processing (OLTP) applications needing multi-datacenter availability without compromising strong consistency. E.g. User identity, retail product catalog, financial data service.

2. Hybrid Transactional/Analytical Processing (HTAP) applications needing real-time analytics on transactional data. E.g User personalization, fraud detection, machine learning.

3. Streaming applications needing to efficiently ingest, analyze and store ever-growing data. E.g. IoT sensor analytics, time series metrics, real time monitoring.

A few such use cases are detailed [here](https://www.yugabyte.com/use-cases/).

### When is YugaByte DB not a good fit?

YugaByte DB is not a good fit for traditional Online Analytical Processing (OLAP) use cases that need complete ad-hoc analytics. Use an OLAP store such as [Druid](http://druid.io/druid.html) or a data warehouse such as [Snowflake](https://www.snowflake.net/).

### Can I deploy YugaByte DB to production?

YugaByte DB is currently in 0.9 beta release with multiple pre-production deployments. Production deployments are not recommended until the planned 1.0 release in 2018.

### Any performance benchmarks available?

[Yahoo Cloud Serving Benchmark (YCSB)](https://github.com/brianfrankcooper/YCSB/wiki) is a popular benchmarking framework for NoSQL databases. We benchmarked YugaByte Cassandra against the standard Apache Cassandra using YCSB. YugaByte outperformed Apache Cassandra by increasing margins as the number of keys (data density) increased across all the 6 YCSB workload configurations. Complete results are available on the [YugaByte Forum](https://forum.yugabyte.com/t/ycsb-benchmark-results-for-yugabyte-and-apache-cassandra/59). 

[Netflix Data Benchmark (NDBench)](https://github.com/Netflix/ndbench) is another publicly available, cloud-enabled benchmark tool for data store systems. We ran NDBench against YugaByte DB for 7 days and observed P99 and P995 latencies that were orders of magnitude less than that of Apache Cassandra. Details are available on the [YugaByte Forum](https://forum.yugabyte.com/t/ndbench-results-for-yugabyte-db/94).

### What about correctness testing?

[Jepsen](https://jepsen.io/) is a widely used framework to evaluate databases’ behavior under different failure scenarios. It allows for a database to be run across multiple nodes, and create artificial failure scenarios, as well as verify the correctness of the system under these scenarios. We have started testing YugaByte DB with Jepsen and will post the results on the [YugaByte Forum](https://forum.yugabyte.com/t/validating-yugabyte-db-with-jepsen/73) as they become available.

## Community Edition 

### Is the Community Edition open source?

Yes, the Community Edition is a completely open source, fully functioning version of YugaByte DB. It is licensed under Apache 2.0 and the source is available on [GitHub](https://github.com/yugabyte/yugabyte-db).

### How do the Community Edition and the Enterprise Edition differ from each other?

[Community Edition](/quick-start/) is the best choice for the startup organizations with strong technical operations expertise looking to deploy YugaByte DB into production with tradtional DevOps tools. 

[Enterprise Edition](/deploy/enterprise-edition/) includes all the features of the Community Edition as well as additional features such as built-in cloud-native operations, enterprise-grade deployment options and world-class support. It is the simplest way to run YugaByte DB in mission-critical production environments with one or more datacenters (across both public cloud and on-premises datacenters).

A more detailed comparison of the two editions is available [here](https://www.yugabyte.com/product/compare/).

## Enterprise Edition 

### What is YugaWare?

YugaWare, shipped as a part of YugaByte Enterprise, is the Admin Console for YugaByte DB. It has a built-in orchestration and monitoring engine for deploying YugaByte DB in any public or private cloud.

### How does the installation process work for YugaByte Enterprise?

YugaWare first needs to be installed on any machine. The next step is to configure YugaWare to work with public and/or private clouds. In the case of public clouds, Yugaware spawns the machines to orchestrate bringing up the data platform. In the case of private clouds, you add the nodes you want to be a part of the data platform into Yugaware. Yugaware would need ssh access into these nodes in order to manage them.

### What are the OS requirements and permissions to run YugaWare, the YugaByte admin console?

Prerequisites for YugaWare are listed [here](/deploy/enterprise-edition/admin-console/#prerequisites).

### What are the OS requirements and permissions to run the YugaByte data nodes?

Prerequisites for the YugaByte data nodes are listed [here](/deploy/enterprise-edition/admin/#prerequisites).

### How are the build artifacts packaged and where are they stored?

The admin console software is packaged as a set of docker container images hosted on [Quay.io](https://quay.io/) container registry and managed by [Replicated](https://www.replicated.com/) management tool. Installation of the admin console starts with installing Replicated on a Linux host. Replicated installs the [docker-engine] (https://docs.docker.com/engine/), the Docker container runtime, and then pulls its own container images the Replicated.com container registry. YugaWare then becomes a managed application of Replicated, which starts by pulling the YugaWare container images from Quay.io for the very first time. Replicated ensures that YugaWare remains highly available as well as allows for instant upgrades by simply pulling the incremental container images associated with a newer YugaWare release. Note that if the host running the admin console does not have Internet connectivity, then a fully air-gapped installation option is also available.

The data node software is packaged into the YugaWare application. YugaWare distributes and installs the data node software on the hosts identified to run the data nodes. Since it's already packaged into existing artifacts, the data node does not require any Internet connectivity.

### How does the Admin Console interact with the YugaByte data nodes?

The YugaWare Admin Console does a password-less ssh to interact with the data nodes. It needs to have the access key file (like a PEM file) uploaded into it via the UI. The setup on each of the data nodes to configure password-less ssh is documented [here](/deploy/#private-cloud-or-on-premises-data-centers).

A REST API is also exposed by the admin console to the end users in addition to the UI as another means of interacting with the data platform.

### Would we have access to the database machines that get spawned in public clouds?

Yes, you would have access to all machines spawned. The machines are spawned by YugaWare. YugaWare runs on your machine in your AWS region/datacenter. If you have configured YugaWare to work with any public cloud like AWS or GCP,  it will spawn YugaByte nodes using your credentials on your behalf. These machines run in your account, but are created and managed by YugaWare on your behalf. You can log on to these machines anytime, and YugaWare will additionally show you some stats graphed into a built in dashboard either per node or per universe.

### How many machines would I need to try out YugaByte against my load?

You would need:  

- One machine to install Yugaware on  

- Minimum as many data nodes as the replication factor. So just one machine for replication factor 1, and 3 machines in case of rf=3  

- A machine to run the load tests on  

Typically you can saturate a database machine (or three in case of replication factor 3) with just one large enough test machine running a synthetic load tester that has a light usage pattern. YugaByte ships some synthetic load-testers with the product which can simulate a few different workloads. For example, one load tester simulates a timeseries/IoT style workload and another does stock-ticker like workload. But if you have a load tester that emulates your planned usage pattern, nothing like it!

### Can we control the properties (such as VPC, IOPS, tenancy etc.) of the machines Yugaware is spinning up? 

Yes, you can control what Yugaware is spinning up. For example: 

- You can choose if Yugaware should spawn a new VPC with peering to the VPC on which application servers are running (to isolate the database machines into a separate VPC) AWS, or ask it to re-use an existing VPC.  

- You to choose dedicated IOPs EBS drives on AWS and specify the number of dedicated IOPS you need.  

In general, we are be able to fill the gaps quickly if we are missing some features. But as a catch all, Yugaware allows creating these machines out of band and import these as "on-premise" install.  

## Comparisons

### How does YugaByte DB compare to the standard Apache Cassandra?

See [YugaByte vs. Apache Cassandra](/architecture/comparisons/#yugabyte-vs-apache-cassandra)

### How does YugaByte DB compare to the standard Redis?

See [YugaByte vs. Redis](/architecture/comparisons/#yugabyte-vs-redis)

### How does YugaByte DB compare against other NoSQL databases such as Apache HBase and MongoDB?

See [YugaByte vs. Apache HBase](/architecture/comparisons/#yugabyte-vs-apache-hbase)

### Why not use a Redis cluser alongside a sharded SQL cluster?

Independent cache and database clusters should be avoided for multiple reasons.

- Development and operational complexity

Sharding has to be implemented at two tiers. Scale out (expanding/shrinking the cluster) has to be re-implemented twice - once at the Redis layer and again for the sharded SQL layer. You need to also worry about resharding the data in the Redis layer. The application has to be aware of two tiers, move data between them, deal with consistency issues, etc. It has to write to Redis and sharded SQL, read from Redis, deal with staleness of data, etc.

- Higher cost

Typically not all the data needs to be cached, and the cache has to adapt to the query pattern. Most people deploying Redis this way end up caching all the data. Some queries need to be answered with low latency from the cache, and others are longer running queries that should not pollute the cache. This is hard to achieve if two systems deal with different access patterns for the same data.

- Cross-DC administration complexity

You need to solve all the failover and failback issues at two layers especially when both sync and async replicas are needed. Additionally, the latency/consistency/throughput tracking needs to be done at two levels, which makes the app deployment architecture very complicated.

## Architecture

### How can YugaByte DB be both CP and HA at the same time?


In terms of the [CAP theorem](https://blog.yugabyte.com/a-for-apple-b-for-ball-c-for-cap-theorem-8e9b78600e6d), YugaByte DB is a Consistent and Partition-tolerant (CP) database. It ensures High Availability (HA) for most practical situations even while remaining strongly consistent. While this may seem to be a violation of the CAP theorem, that is not the case. CAP treats availability as a binary option whereas YugaByte DB treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available). 

- During network partitions or node failures, the replicas of the impacted tablets (whose leaders got partitioned out or lost) form two grounps: a majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). The replicas in the majority partition elect a new leader among themselves in a matter of seconds and are ready to accept new writes after the leader election completes. For these few seconds till the new leader is elected, the DB is unable to accept new writes given the design choice of prioritizing consistency over availabililty. All the leader replicas in the minority partition lose their leadership during these few seconds and hence become followers. 

- Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. **Multi-active availability** refers to YugaByte DB's ability to serve writes on any node of a non-partitioned cluster and reads on any node of a partitioned cluster.

- The above approach obviates the need for any unpredictable background anti-entropy operations as well as need to establish quorum at read time. As shown in the [YCSB benchmarks against Apache Cassandra](https://forum.yugabyte.com/t/ycsb-benchmark-results-for-yugabyte-and-apache-cassandra-again-with-p99-latencies/99), YugaByte DB delivers predictable p99 latencies as well as 3x read throughput that is also timeline-consistent (given no quorum is needed at read time).

YugaByte DB's architecture is similar to that of [Google Cloud Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) which is also a CP database with high write availability. While Google Cloud Spanner leverages Google's proprietary network infrastructure, YugaByte DB is designed work on commodity infrastructure used by most enterprise users.

### Why is a group of YugaByte DB nodes called a `universe` instead of the more commonly used term `clusters`?

The YugaByte universe packs a lot more functionality that what people think of when referring to a cluster. In fact, in certain deployment choices, the universe subsumes the equivalent of multiple clusters and some of the operational work needed to run these. Here are just a few concrete differences, which made us feel like giving it a different name would help earmark the differences and avoid confusion.

- A YugaByte universe can move into new machines/AZs/Regions/DCs in an online fashion, while these primitives are not associated with a traditional cluster.

- It is very easy to setup multiple async replicas with just a few clicks (in the Enterprise edition). This is built into the universe as a first-class operation with bootstrapping of the remote replica and all the operational aspects of running async replicas being supported natively. In the case of traditional clusters, the source and the async replicas are independent clusters. The user is responsible for maintaining these separate clusters as well as operating the replication logic.

- Failover to async replicas as the primary data and failback once the original datacenter is up and running are both natively supported within a universe.

## Apache Cassandra Query Language (CQL)

### Do INSERTs do “upserts” by default? How do I insert data only if it is absent?

By default, inserts overwrite data on primary key collisions. So INSERTs do an upsert. This an intended CQL feature. In order to insert data only if the primary key is not already present,  add a clause "IF NOT EXISTS" to the INSERT statement. This will cause the INSERT fail if the row exists.

Here is an example from CQL:

```sql
INSERT INTO mycompany.users (id, lastname, firstname) 
   VALUES (100, ‘Smith’, ‘John’) 
IF NOT EXISTS;
```

### Can I have collection data types in the partition key? Will I be able to do partial matches on that collection data type?

Yes, you can have collection data types as primary keys as long as they are marked FROZEN. Collection types that are marked `FROZEN` do not support partial matches.

### What is the difference between a `COUNTER` type and `INTEGER` types?

Unlike Apache Cassandra, YugaByte COUNTER type is almost the same as INTEGER types. There is no need of lightweight transactions requiring 4 round trips to perform increments in YugaByte - these are efficiently performed with just one round trip.



