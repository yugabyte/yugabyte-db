---
title: Azure Cosmos DB
linkTitle: Azure Cosmos DB
description: Azure Cosmos DB
aliases:
  - /comparisons/azure-cosmos/
menu:
  latest:
    identifier: azure-cosmos
    parent: comparisons
    weight: 1110
---

YugaByte DB's multi-model, multi-API and tunable read latency approach is similar to that of [Azure Cosmos DB](https://azure.microsoft.com/en-us/blog/a-technical-overview-of-azure-cosmos-db/). However, it's underlying storage and replication architecture is much more resilient than that of Cosmos DB. Support for distributed ACID transactions and global consistency across multi-region and multi-cloud deployments makes it much more suitable for enterprises building distributed OLTP apps and hesitant to getting locked into proprietary databases such as Cosmos DB.

A post on our blog titled [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and YugaByte DB](https://medium.com/p/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db-ce720e07c0fd) goes through some of above topics in more detail.
