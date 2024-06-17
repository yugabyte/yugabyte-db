---
title: Distributed Data modeling
linkTitle: Data modeling
description: Learn to develop YugabyteDB applications
image: fa-sharp fa-light fa-objects-column
aliases:
  - /preview/develop/learn/data-modeling-ysql/
  - /preview/develop/data-modeling-ysql/
menu:
  preview:
    identifier: data-modeling
    parent: develop
    weight: 100
type: indexpage
---

Data modeling is the process of defining the structure, organization, and relationships of data in a database. In a distributed SQL database, this process becomes even more crucial due to the complexities introduced by data distribution, replication, and consistency. To fully leverage the benefits offered by YugabyteDB, you need to approach data modeling with a distributed mindset. Data modeling for distributed SQL databases requires a careful balance of theoretical principles and practical considerations.

## Organization

In YugabyteDB, data is stored as rows and columns in tables; tables are organized under schemas and databases.

{{<lead link="../../../explore/ysql-language-features/databases-schemas-tables">}}
To understand how to create and manage tables, schemas, and databases, see [Schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables).
{{</lead>}}

## Sharding

In YugabyteDB, table data is split into tablets, and distributed across multiple nodes in the cluster. Applications can connect to any node for storing and retrieving data. Because reads and writes can span multiple nodes, it's crucial to consider how table data is sharded and distributed when modeling your data. To design your tables and indexes for fast retrieval and storage in YugabyteDB, you first need to understand the [data distribution](../../../explore/going-beyond-sql/data-sharding) schemes: Hash and Range sharding.

{{<lead link="../../../explore/going-beyond-sql/data-sharding">}}
To learn more about data distribution schemes, see [Configurable data sharding](../../../explore/going-beyond-sql/data-sharding).
{{</lead>}}

## Primary keys

The primary key is the unique identifier for each row in the table. The distribution and ordering of table data depends on the primary key.

{{<lead link="./primary-keys">}}
To design optimal primary keys for your tables, see [Primary keys](./primary-keys).
{{</lead>}}

## Secondary indexes

Indexes provide alternate access patterns for queries not involving the primary key of the table. With the help of an index, you can improve the access operations of your queries.

{{<lead link="./secondary-indexes">}}
To design optimal indexes for faster lookup, see [Secondary indexes](./secondary-indexes).
{{</lead>}}

## Hot shards

In distributed systems, a hot-spot or hot-shard refers to a node that is overloaded with queries due to disproportionate traffic compared to other nodes in the cluster.

{{<lead link="./hot-shards">}}
To understand the hot-shard problem and solutions to overcome the issue, see [Hot shards](./hot-shards).
{{</lead>}}

## Table partitioning

When the data in tables keep growing, you can partition the tables for better performance and enhanced data management. Partitioning also makes it easier to drop older data by dropping partitions. In YugabyteDB, you can also use partitioning with Tablespaces to improve latency in multi-region scenarios and adhere to data residency laws like GDPR.

{{<lead link="./partitioning">}}
To understand partitioning in YugabyteDB, see [Table partitioning](./partitioning).
{{</lead>}}
