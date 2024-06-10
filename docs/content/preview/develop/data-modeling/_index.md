---
title: Distributed Data modeling
linkTitle: Data modeling
description: Learn to develop YugabyteDB applications
image: fa-sharp fa-light fa-objects-column
aliases:
- /preview/develop/learn/data-modeling-ysql
- /preview/develop/data-modeling-ysql
menu:
  preview:
    identifier: data-modeling
    parent: develop
    weight: 100
type: indexpage
---

Data modeling is the process of defining the structure, organization, and relationships of data within a database. In a distributed SQL database, this process becomes even more crucial due to the complexities introduced by data distribution, replication, and consistency. To fully leverage the benefits offered by YugabyteDB, you need to approach data modeling with a distributed mindset. This guide will you understand that data modeling for distributed SQL databases requires a careful balance of theoretical principles and practical considerations.

## Organization

In YugabyteDB, data is stored as rows and columns in tables, which are organized under schemas and databases.

{{<lead link="../../../explore/ysql-language-features/databases-schemas-tables">}}
To understand more about creating and managing tables, schemas, and databases, see [Schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables).
{{</lead>}}

## Sharding

In YugabyteDB, table data is split into tablets and distributed across multiple nodes in the cluster, allowing applications to connect to any node for storing and retrieving data. Because reads and writes can span multiple nodes, it's crucial to consider how table data is sharded and distributed when modeling your data.

{{<lead link="../../../explore/going-beyond-sql/data-sharding">}}
To design your tables and indexes for fast retrieval and storage in YugabyteDB, you first need to understand the two [data distribution](../../../explore/going-beyond-sql/data-sharding) schemes, Hash and Range sharding, in detail.
{{</lead>}}

## Primary keys

Primary key is the unique identifier for each row in the table. The disribution and ordering of table data is depdendent on the primary key.

{{<lead link="./primary-keys">}}
To design optimal primary keys for your tables, see [Primary keys](./primary-keys)
{{</lead>}}

## Secondary indexes

Indexes provide alternate access patterns for queries not involving the primary key of the table. With the help of an index, you can improve the access operations of your queries.

{{<lead link="./secondary-indexes">}}
To design optimal indexes for faster lookup, see [Secondary indexes](./secondary-indexes)
{{</lead>}}

## Hot shards

In distributed systems, the hot-spot or hot-shard problem refers to the situation where one node gets overloaded with queries due to dispropotionate traffic compared to other nodes in the cluster.

{{<lead link="./hot-shards">}}
To understand the hot-shard problem and the solutions to overcome the issue, see [Hot shards](./hot-shards)
{{</lead>}}

## Table partitioning

When the data in tables keep growing, it is better to partition them for better performance and enahnced data management. It will be easy to drop older data just by dropping partitions. In YugabyteDB, you can use partitioning along with Tablespaces to improve latency in multi-region scenario and adhere to compliance laws like GDPR.

{{<lead link="./partitioning">}}
To understand how partitioning can be useful, see [Table partitioning](./partitioning)
{{</lead>}}

## Data modeling in YCQL

YCQL is YugabyteDB's Cassandra like interface. Tables are organized into Keyspaces similar to namespaces in SQL.

{{<lead link="./data-modeling-ycql">}}
To understand how model your data for YCQL, see [YCQL - Data modeling](./data-modeling-ycql)
{{</lead>}}
