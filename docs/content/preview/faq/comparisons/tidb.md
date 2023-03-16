---
title: Compare TiDB with YugabyteDB
headerTitle: TiDB
linkTitle: TiDB
description: Compare TiDB database with YugabyteDB.
aliases:
  - /comparisons/tidb/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-tidb
    weight: 1076
type: docs
---

PingCap's TiDB is a MySQL-compatible distributed database built on TiKV, and takes design inspiration from Google Spanner and Apache HBase. While its sharding and replication architecture are similar to that of Spanner, it follows a very different design for multi-shard transactions. TiDB uses Google Percolator as the inspiration for its multi-shard transaction design. This choice essentially makes TiDB unfit for deployments with geo-distributed writes since the majority of transactions in a random-access OLTP workload will now experience high WAN latency when acquiring a timestamp from the global timestamp oracle running in a different region. Additionally, TiDB lacks support for critical relational data modeling constructs such as foreign key constraints and Serializable isolation level.

## Relevant blog posts

The following posts cover some more details around how YugabyteDB differs from TiDB.

- [What is Distributed SQL?](https://www.yugabyte.com/blog/what-is-distributed-sql/)
- [Implementing Distributed Transactions the Google Way: Percolator vs. Spanner](https://www.yugabyte.com/blog/implementing-distributed-transactions-the-google-way-percolator-vs-spanner/)
