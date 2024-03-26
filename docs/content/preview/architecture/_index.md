---
title: Architecture
headerTitle: Architecture
linkTitle: Architecture
description: Learn about the YugabyteDB architecture, including query, transactions, sharding, replication, and storage layers.
image: /images/section_icons/index/architecture.png
headcontent: YugabyteDB architecture including the query, transactions, sharding, replication, and storage layers.
menu:
  preview:
    identifier: architecture
    parent: reference
    weight: 1050
type: indexpage
---

YugabyteDB is a distributed database that seamlessly combines the principles of distributed systems, where multiple machines collaborate, with the familiar concepts of traditional databases, where data is organized in tables with standard interfaces for reading and writing data.

Unlike traditional centralized databases, YugabyteDB is designed to manage and process data across multiple nodes or servers, ensuring consistency, high availability, scalability, fault tolerance and other [design goals](design-goals/).

## Layered architecture

In general, the operations in YugabyteDB are split logically into 2 layers, the query layer and the storage layer. The query layer is responsible for handling user requests and sending the requests to the right data. The storage layer is responsible for optimally storing the data on disk and manages replication and consistency.

![YugabyteDB Layered Architecture](/images/architecture/layered-architecture.png)

## Query layer

For operating (CRUD) on the data that is split and stored across multiple machines, YugabyteDB provides two APIs YSQL and YCQL via the query layer that takes in the user query and sends or fetches data to and from the right set of tablets.

{{<tip>}}
To understand how the query layer is designed, see [Query layer](query-layer/)
{{</tip>}}

## Storage layer

The tablet data is optimally stored and managed by DocDB, a document store that has been built on top of RocksDB for higher performance and persistence.

{{<tip>}}
To understand how data storage works in YugabyteDB, see [DocDB](docdb-storage/)
{{</tip>}}

## Sharding

YugabyteDB splits table data into smaller pieces called tablets so that the data can be stored in parts across multiple machines. The mapping of a row to a tablet is deterministic and this process is known as Sharding.

{{<tip>}}
To learn more about the various sharding schemes, see [Sharding](docdb-sharding/)
{{</tip>}}

## Replication

These tablets are replicated for high availability and fault tolerance. Each tablet has a leader that is responsible for consistent reads and writes to the data of the tablet and a few followers. The replication is done using the RAFT protocol to ensure consistency of data across the leader and followers.

{{<tip>}}
To understand how replication works, see [Replication](docdb-replication/)
{{</tip>}}

## Transactions

Transactions are a set of operations (CRUD) that are executed atomically with the option to roll back all actions if any one operation fails.

{{<tip>}}
To understand how transactions work in YugabyteDB, see [Transactions](transactions/)
{{</tip>}}

<div class="row">


  {{<index/item
    title="Core functions"
    body="Universe and table creation, the I/O path reading and writing, and high availability."
    href="core-functions/"
    icon="/images/section_icons/architecture/core_functions.png">}}

  {{<index/item
    title="Layered architecture"
    body="YugabyteDB architecture overview."
    href="layered-architecture/"
    icon="/images/section_icons/architecture/concepts.png">}}


</div>
