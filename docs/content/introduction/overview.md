---
date: 2016-03-08T21:07:13+01:00
title: Overview
weight: 3
---

## What is YugaByte DB?

YugaByte DB is an open source, cloud-native database for mission-critical enterprise applications. It is meant to be a system-of-record/authoritative database that applications can rely on for correctness and availability. It allows applications to easily scale up and scale down in the cloud, on-premises or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugaByte currently supports **Apache Cassandra Query Language** & its client drivers natively. In addition, it also supports an automatically sharded, clustered & elastic **Redis-as-a-Database** in a Redis driver compatible manner. **Distributed transactions** to support **strongly consistent secondary indexes**, multi-table/row ACID operations and SQL support is on the roadmap.

## What makes YugaByte DB unique?

### Purpose-built for mission-critical applications

Mission-critical applications have a strong need for data correctness and high availability. They are typically composed of microservices with diverse workloads such as key/value, flexible schema, graph or relational. The access patterns vary as well. SaaS services or mobile/web applications keeping customer records, order history or messages need zero-data loss, geo-replication, low-latency reads/writes and a consistent customer experience. While fast data infrastructure use cases (such as IoT, finance, timeseries data) need near real-time & high-volume ingest, low-latency reads, and native integration with analytics frameworks like Apache Spark.

YugaByte offers polyglot persistence to power these diverse workloads and access patterns in a unified database, while providing strong correctness guarantees and high availability. You are no longer forced to create infrastructure silos for each workload or choose between different flavors SQL and NoSQL databases. YugaByte breaks down the barrier between SQL and NoSQL by offering both.

### Cloud-native agility

Another theme common across these microservices is the move to a cloud-native architecture, be it on the public cloud, on-premises or hybrid environment. The primary driver for this move is making the infrastructure agile, scalable, re-configurabile with zero downtime, geo-distributed and portable across clouds. While the container ecosystem led by Docker & Kubernetes has enabled enterprises to realize this vision for the stateless tier, the data tier has remained a big challenge.  YugaByte is purpose-built to address these challenges, but for the data tier, and serves as the stateful complement to containers.