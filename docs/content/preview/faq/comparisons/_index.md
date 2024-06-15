---
title: Compare YugabyteDB with other distributed SQL and NoSQL databases
linkTitle: Comparisons
headerTitle: Compare YugabyteDB to other databases
description: Learn how YugabyteDB compares with other operational SQL and NoSQL databases.
image: /images/section_icons/index/comparisons.png
headcontent: See how YugabyteDB compares with other operational databases in the distributed SQL and NoSQL categories. For a detailed comparison, click the database name.
aliases:
  - /comparisons/
  - /preview/faq/comparisons/references
menu:
  preview_faq:
    identifier: comparisons
    parent: comparisons-home
    weight: 100
cascade:
  unversioned: true
type: indexpage
---

## Distributed SQL databases

| Feature | [CockroachDB](cockroachdb/) | [TiDB](tidb/) | [Vitess](vitess/) | [Amazon Aurora](amazon-aurora/) | [Google Cloud Spanner](google-spanner/) | YugabyteDB |
| :------ | :-------------------------: | :-----------: | :---------------: | :-----------------------------: | :-------------------------------------: | :--------: |
| Horizontal write scalability (with auto-sharding and rebalancing) | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Automated failover &amp; repair | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Distributed ACID transactions | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| SQL Foreign Keys | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| SQL Joins | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Serializable isolation level | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Global consistency across multi-DC/regions | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-exclamation"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Follower reads| <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> |
| Built-in enterprise features (such as CDC) | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| SQL compatibility | PostgreSQL | MySQL | MySQL | MySQL, PostgreSQL | Proprietary | PostgreSQL |
| Open Source | <i class="fa-solid fa-xmark"></i> | Apache 2.0 | Apache 2.0 |  <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | Apache 2.0 |

## NoSQL databases

| Feature | [MongoDB](mongodb/) | [FoundationDB](foundationdb/) | [Apache Cassandra](cassandra/) | [Amazon DynamoDB](amazon-dynamodb/) | [MS Azure CosmosDB](azure-cosmos/)| YugabyteDB |
| :------ | :-----------------: | :---------------------------: | :----------------------------: | :---------------------------------: | :-------------------------------: | :--------: |
| Horizontal write scalability (with auto-sharding and rebalancing) | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Automated failover &amp; repair | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| Distributed ACID transactions | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> |
| Consensus-driven, strongly-consistent replication | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> |
| Strongly-consistent secondary indexes | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> |
| Multiple read consistency levels | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> | <i class="fa-solid fa-check"></i> |
| High data density| <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-check"></i> |
| API | MongoDB QL | Proprietary KV, MongoDB QL | Cassandra QL | Proprietary KV, Document | Cassandra QL, MongoDB QL | Yugabyte Cloud QL w/ native document modeling|
| Open Source | <i class="fa-solid fa-xmark"></i> | Apache 2.0 | Apache 2.0 | <i class="fa-solid fa-xmark"></i> | <i class="fa-solid fa-xmark"></i> | Apache 2.0|

{{< note title="Note" >}}

The <i class="fa-solid fa-check"></i>, <i class="fa-solid fa-xmark"></i>, or <i class="fa-solid fa-exclamation"></i> with respect to any particular feature of a third-party database is based on our best effort understanding from publicly available information. Readers are always recommended to perform their own independent research to understand the finer details.

{{< /note >}}
