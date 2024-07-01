---
title: DocDB replication layer
headerTitle: DocDB replication layer
linkTitle: Replication
description: Learn how synchronous and asynchronous replication work in DocDB, including advanced features like xCluster replication and read replicas.
image: fa-sharp fa-light fa-copy
headcontent: Learn how synchronous and asynchronous replication work in DocDB.
menu:
  preview:
    identifier: architecture-docdb-replication
    parent: architecture
    weight: 800
type: indexpage
---

Table data is split into [tablets](../key-concepts#tablet) and managed by [DocDB](../docdb). By default, each tablet is synchronously replicated as per the [replication factor](../key-concepts#replication-factor-rf), using the [Raft](./raft) algorithm across various nodes to ensure data consistency, fault tolerance, and high availability. The replication layer is a critical component that determines how data is replicated, synchronized, and made consistent across the distributed system. In this section, you can explore the key concepts and techniques used in the replication layer of YugabyteDB.

## Raft consensus protocol

At the heart of YugabyteDB is Raft consensus protocol that ensures the replicated data remains consistent across all the nodes. Raft is designed to be a more understandable alternative to the complex Paxos protocol. It works by electing a leader node that is responsible for managing the replicated log and coordinating the other nodes.

{{<lead link="./raft">}}
To understand the different concepts in the consensus protocol, see [Raft](./raft).
{{</lead>}}

## Synchronous Replication

YugabyteDB ensures that all writes are replicated to a majority of the nodes before the write is considered complete and acknowledged to the client. This provides the highest level of data consistency, as the data is guaranteed to be durable and available on multiple nodes. YugabyteDB's synchronous replication architecture is inspired by [Google Spanner](https://research.google.com/archive/spanner-osdi2012.pdf).

{{<lead link="./replication">}}
To understand how replication works, see [Synchronous replication](./replication).
{{</lead>}}

## xCluster

Asynchronous replication, on the other hand, does not wait for writes to be replicated to all the nodes before acknowledging the client. Instead, writes are acknowledged immediately, and the replication process happens in the background. Asynchronous replication provides lower latency for write operations, as the client does not have to wait for the replication to complete. However, it comes with the trade-off of potentially lower consistency across universes, as there may be a delay before the replicas are fully synchronized.

In YugabyteDB, you can use xCluster to set up asynchronous replication between 2 different distant [universes](../key-concepts#universe) either in a unidirectional or bi-directional manner.

{{<lead link="./async-replication">}}
To understand how asynchronous replication between 2 universes works, see [xCluster](./async-replication).
{{</lead>}}

## Read replica

Read replicas are effectively in-universe asynchronous replicas. It is a optional cluster that you can add on to an existing cluster which can help you improve read latency for users located far away from your [primary cluster](../key-concepts#primary-cluster).

{{<lead link="./read-replicas">}}
To understand how read replicas work, see [Read replicas](./read-replicas).
{{</lead>}}

## Change Data Capture (CDC)

CDC is a technique used to track and replicate changes to the data. CDC systems monitor the database's transaction log and capture any changes that occur. These changes are then propagated to external systems or replicas using connectors.

CDC is particularly beneficial in scenarios where real-time data synchronization is required, such as data warehousing, stream processing, and event-driven architectures. It allows the replicated data to be kept in sync without the need for full table replication, which can be more efficient and scalable.

{{<lead link="./change-data-capture">}}
To understand how CDC works, see [CDC](./change-data-capture).
{{</lead>}}
