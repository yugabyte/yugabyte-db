---
title: Architecture
headerTitle: Architecture
linkTitle: Architecture
description: Learn about the YugabyteDB architecture, including query, transactions, sharding, replication, and storage layers.
image: fa-sharp fa-thin fa-puzzle
headcontent: Internals of query, transactions, sharding, replication, and storage layers
menu:
  stable:
    identifier: architecture
    parent: reference
    weight: 1050
type: indexpage
---

YugabyteDB is a distributed database that seamlessly combines the principles of distributed systems, where multiple machines collaborate, with the familiar concepts of traditional databases, where data is organized in tables with standard interfaces for reading and writing data.

Unlike traditional centralized databases, YugabyteDB is designed to manage and process data across multiple nodes or servers, ensuring resiliency, consistency, high availability, scalability, fault tolerance, and other [design goals](design-goals/).

{{<lead link="./key-concepts/">}}
Check out YugabyteDB [key concepts](./key-concepts/) for your quick reference.
{{</lead>}}

## Layered architecture

In general, operations in YugabyteDB are split logically into 2 layers, the query layer and the storage layer. The query layer is responsible for handling user requests and sending the requests to the right data. The storage layer is responsible for optimally storing the data on disk and managing replication and consistency.

![YugabyteDB Layered Architecture](/images/architecture/layered-architecture.png)

## Query layer

For operating (CRUD) on the data that is split and stored across multiple machines, YugabyteDB provides two APIs, YSQL and YCQL. The query layer takes the user query submitted via the API and sends or fetches data to and from the right set of tablets.

{{<lead link="query-layer/">}}
To understand how the query layer is designed, see [Query layer](query-layer/).
{{</lead>}}

## Storage layer

The tablet data is optimally stored and managed by DocDB, a document store that has been built on top of RocksDB for higher performance and persistence.

{{<lead link="docdb/">}}
To understand how data storage works in YugabyteDB, see [DocDB](docdb/).
{{</lead>}}

## Sharding

YugabyteDB splits table data into smaller pieces called tablets so that the data can be stored in parts across multiple machines. The mapping of a row to a tablet is deterministic and this process is known as sharding.

{{<lead link="docdb-sharding/">}}
To learn more about the various sharding schemes, see [Sharding](docdb-sharding/).
{{</lead>}}

## Replication

Tablets are replicated for resiliency, high availability, and fault tolerance. Each tablet has a leader that is responsible for consistent reads and writes to the data of the tablet and a few followers. The replication is done using the Raft protocol to ensure consistency of data across the leader and followers.

{{<lead link="docdb-replication/">}}
To understand how replication works, see [Replication](docdb-replication/).
{{</lead>}}

## Transactions

Transactions are a set of operations (CRUD) that are executed atomically with the option to roll back all actions if any operation fails.

{{<lead link="transactions/">}}
To understand how transactions work in YugabyteDB, see [Transactions](transactions/).
{{</lead>}}

## Master server

The master service acts a catalog manager and cluster orchestrator, and manages many background tasks.

{{<lead link="./yb-master">}}
For more details, see [YB-Master](./yb-master).
{{</lead>}}

## TServer

YugabyteDB splits table data into tablets. These tablets are maintained and managed on each node by the TServer.

{{<lead link="./yb-tserver">}}
For more details, see [YB-TServer](./yb-tserver).
{{</lead>}}
