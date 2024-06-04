---
title: Distributed Data modeling
linkTitle: Data modeling
description: Learn to develop YugabyteDB applications
image: fa-sharp fa-light fa-objects-column

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

In YugabyteDB, the sharding and ordering of data in the tables and indexes is governed by the primary key of the table and index respectively.