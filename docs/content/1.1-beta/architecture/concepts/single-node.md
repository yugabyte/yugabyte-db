---
title: Single Node
linkTitle: Single Node
description: Single Node
aliases:
  - /architecture/concepts/single-node/
menu:
  1.1-beta:
    identifier: architecture-single-node
    parent: architecture-concepts
    weight: 920
---

Every YugaByte DB node is comprised of 2 layers, **YBase** and **YQL**. YBase, the core data fabric, is a highly available, distributed system with [strong write consistency](../replication/#strong-write-consistency), [tunable read consistency](../replication/#tunable-read-consistency), and [an advanced log-structured row/document-oriented storage](../persistence/). It includes several optimizations for handling ever-growing datasets efficiently. [YQL](../yql/) is the upper/edge layer that has the API specific aspects - for example, the server-side implementation of Apache Cassandra Query Language and Redis protocols, and the corresponding query/command compilation and run-time (data type representations, built-in operations, etc.).

![YugaByte Architecture](/images/architecture/architecture.png)

In terms of the traditional [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte DB is a CP database (consistent and partition tolerant), but achieves very high availability. The architectural design of YugaByte is similar to Google Cloud Spanner, which is also a CP system. The description about Spanner [here](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) is just as valid for YugaByte DB. The key takeaway is that no system provides 100% availability, so the pragmatic question is whether or not the system delivers availability that is so high that most users no longer have to be concerned about outages. For example, given there are many sources of outages for an application, if YugaByte DB is an insignificant contributor to its downtime, then users are correct to not worry about it.
