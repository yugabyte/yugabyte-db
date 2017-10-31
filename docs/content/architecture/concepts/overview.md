---
date: 2016-03-09T20:08:11+01:00
title: Overview
weight: 22
---

YugaByte is a cloud-native database for mission-critical enterprise applications. It is meant to be a system-of-record/authoritative database that applications can rely on for correctness and availability. It allows applications to easily scale up and scale down in the cloud, on-premises or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugaByte currently supports **Apache Cassandra Query Language** & its client drivers natively. In addition, it also supports an automatically sharded, clustered & elastic **Redis-as-a-Database** in a Redis driver compatible manner. **Distributed transactions** to support **strongly consistent secondary indexes**, multi-table/row ACID operations and SQL support is on the roadmap.

![YugaByte Architecture](/images/architecture.png)

All of the above data models are powered by a **common underlying core** (aka **YBase**) - a highly available, distributed system with strong write consistency, tunable read consistency, and an advanced log-structured row/document-oriented storage model with several optimizations for handling ever-growing datasets efficiently. Only the upper/edge layer (the YugaByte Query Layer or **YQL**) of the servers have the API specific aspects - for example, the server-side implementation of Apache Cassandra and Redis protocols, and the corresponding query/command compilation and run-time (data type representations, built-in operations, etc.).

In terms of the traditional [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte is a CP database (consistent and partition tolerant), but achieves very high availability. The architectural design of YugaByte is similar to Google Cloud Spanner, which is also technically a CP system. The description about Spanner [here](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) is just as valid for YugaByte. The point is that no system provides 100% availability, so the pragmatic question is whether or not the system delivers availability that is so high that most users no longer have to be concerned about outages. For example, given there are many sources of outages for an application, if YugaByte is an insignificant contributor to its downtime, then users are correct to not worry about it.
