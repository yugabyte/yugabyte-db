---
title: Compare Amazon DynamoDB with YugabyteDB
headerTitle: Amazon DynamoDB
linkTitle: Amazon DynamoDB
description: Compare Amazon DynamoDB with YugabyteDB.
aliases:
  - /comparisons/amazon-dynamodb/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-dynamodb
    weight: 1105
type: docs
---

## Astronomical AWS bills and slow releases

Amazon DynamoDB is a fully-managed NoSQL database offered by Amazon Web Services. While it works very well for smaller scale applications, the limitations it poses in the context of larger scale applications are not well understood.

![Amazon DynamoDB Issues](/images/comparisons/amazon-dynamodb-issues.png)

Our post [11 Things You Wish You Knew Before Starting with DynamoDB](https://www.yugabyte.com/blog/11-things-you-wish-you-knew-before-starting-with-dynamodb/) highlights the above items in better detail.

## YugabyteDB gives 3x agility at 1/10th cost

YugabyteDB is an open source [multi-API/multi-model](https://www.yugabyte.com/blog/polyglot-persistence-vs-multi-api-multi-model-which-one-makes-multi-cloud-easy) database with transactional consistency, low latency, and geo-distribution built into the core of a common storage engine. It is Consistent and Partition-tolerant (CP), and provides native support for JSONB, high performance secondary indexes, cloud-native operational ease, and the ability to handle high data density. Not only is it an excellent alternative to Amazon DynamoDB, it also negates the need for a separate RDBMS and cache.

![Amazon DynamoDB issues](/images/comparisons/yugabyte-db-beats-amazon-dynamodb.png)

Our post [DynamoDB vs MongoDB vs Cassandra for Fast Growing Geo-Distributed Apps](https://www.yugabyte.com/blog/dynamodb-vs-mongodb-vs-cassandra-for-fast-growing-geo-distributed-apps/) further details the differences between YugabyteDB and DynamoDB.
