---
title: Data modeling strategies
linkTitle: Data modeling
description: Data modeling strategies for migrating data using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: data-modeling-voyager
    parent: reference-voyager
    weight: 101
type: docs
---

Before performing migration from your source database to YugabyteDB, review your sharding strategies.

YugabyteDB supports two ways to shard data: HASH and RANGE. HASH is the default, as it is typically better suited for most OLTP applications. For more information, refer to [Hash and range sharding](../../../architecture/docdb-sharding/sharding/). When exporting a PostgreSQL database, be aware that if you want RANGE sharding, you must call it out in the schema creation.

For most workloads, it is recommended to use HASH partitioning because it efficiently partitions the data, and spreads it evenly across all nodes.

RANGE sharding can be advantageous for particular use cases, such as time series. When querying data for specific time ranges, using RANGE sharding to split the data into the specific time ranges will help improve the speed and efficiency of the query.

Additionally, you can use a combination of HASH and RANGE sharding for your [primary key](../../../explore/ysql-language-features/indexes-constraints/primary-key-ysql/) by choosing a HASH value as the [partition key](../../../develop/learn/data-modeling-ycql/#partition-key-columns-required), and a RANGE value as the [clustering key](../../../develop/learn/data-modeling-ycql/#clustering-key-columns-optional).

## Learn more

- [Data type mapping](../datatype-mapping-mysql)
