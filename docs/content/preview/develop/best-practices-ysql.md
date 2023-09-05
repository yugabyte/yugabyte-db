---
title: Best practices for YSQL applications
headerTitle: Best practices - YSQL
linkTitle: Best practices
description: Tips and tricks to build YSQL applications for high performance and availability
headcontent: Tips and tricks to build YSQL applications for high performance and availability
menu:
  preview:
    identifier: best-practices-ysql
    parent: develop
    weight: 570
aliases:
  - /preview/develop/best-practices/
type: docs
---

{{<api-tabs>}}

## Faster reads with Covering Indexes

When a query uses an index to look up rows faster, the columns that are not present in the index are fetched from the original table. This will result in additional round trips to the main table leading to increased latency.

Use [Covering Indexes](../../../explore/indexes-constraints/covering-index-ysql/) to store all the needed columns needed for your queries in the index. This will convert a standard Index-Scan to an [Index-Only-Scan](https://dev.to/yugabyte/boosts-secondary-index-queries-with-index-only-scan-5e7j)

{{<tip>}}
For more details see, [Avoid Trips to the Table](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes)
{{</tip>}}

## Distinct Keys with Unique Indexes

If you need values in some of the columns to be unique, you can specify your index as `UNIQUE`.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a NULL value is treated as a distinct value, you can have multiple NULL values in a column with a unique index.

{{<tip>}}
For more details see, [Unique Indexes](../../indexes-constraints/unique-index-ysql/)
{{</tip>}}

## Faster sequences with Server-Level Caching

Sequences in databases automatically generate incrementing numbers, perfect for generating unique values like order numbers, user IDs, check numbers, etc. They prevent multiple application instances from concurrently generating duplicate values. However, generating sequences on a database that is spread across regions could have a latency impact on your applications.

Enable [server-level caching](http://localhost:1313/preview/api/ysql/exprs/func_nextval/#caching-values-on-the-yb-tserver) to improve the speed of your sequences tremendously. This will also avoid the discarding of many sequence values when an application disconnects.

{{<tip>}}
For a live demo see, [Scaling Sequences](https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76)
{{</tip>}}

## Fast single-row transactions

Common scenarios of updating rows and fetching the results in multiple statements will lead to multiple round-trips between the application and server. In many cases, such statements if rewritten as single statements using `RETURNING` will lead to lower latencies as YugabyteDB has optimizations to make single statements faster. For example, these `3` statements:

```sql
SELECT v FROM txndemo WHERE k=1 FOR UPDATE;
UPDATE txndemo SET v = v + 3 WHERE k=1;
SELECT v FROM txndemo WHERE k=1;
```

can be re-written as:

```sql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

{{<tip>}}
For more details see, [Fast single-row transactions](../../learn/transactions/transactions-performance-ysql/#fast-single-row-transactions)
{{</tip>}}

## Delete older data easily with partitioning

Use [Table partitioning](../../explore/ysql-language-features/advanced-features/partitions/) to split your data into multiple partitions according to date so that you can quickly delete older data by simply dropping the partition.

{{<tip>}}
For more details see, [Partition data by time](../common-patterns/timeseries/partitioning-by-time/)
{{</tip>}}


## Load balance and Failover using YugabyteDB drivers

YugabyteDB’s [Smart Driver for YSQL](../../drivers-orms/smart-drivers/) provides advanced cluster-aware load-balancing capabilities that will enable your applications to send requests to multiple nodes in the cluster just by connecting to one node. You can also set a fallback hierarchy by assigning priority to specific regions and ensuring that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails

{{<tip>}}
For more details see, [Load Balancing with Smart Driver](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver)
{{</tip>}}


## Leverage connection pooling in the YCQL client

A single client (for example, a multi-threaded application) should ideally use a single cluster object. The single cluster object typically holds underneath the covers a configurable number of connections to YB-TServers. Typically 1 or 2 connections per YB-TServer suffices to serve even 64-128 application threads. The same connection can be used for multiple outstanding requests, also known as multiplexing.

See also [Connection pooling](https://docs.datastax.com/en/developer/java-driver/4.6/manual/core/pooling/) in the DataStax Java Driver documentation.

## Use prepared statements

Whenever possible, use prepared statements to ensure that YugabyteDB partition-aware drivers can route queries to the tablet leader, to improve throughput, and to eliminate the need for a server to parse the query on each operation.

## Use batching for higher throughput

Use batching for writing a set of operations. This will send all operations in a single RPC call instead of using multiple RPC calls, one per operation. Each batch operation has higher latency compared to single rows operations but has higher throughput overall.

## Column and row sizes

For consistent latency/performance, keep columns in the 2 MB range or less.

Big columns add up when selecting multiple columns or full rows. For consistent latency and performance, keep the size of individual rows in the 32 MB range or less.

## Don't use big collections

Collections are designed for storing small sets of values that are not expected to grow to arbitrary size (such as phone numbers or addresses for a user rather than posts or messages). While collections of larger sizes are allowed, they may have a significant impact on performance for queries involving them. In particular, some list operations (insert at an index and remove elements) require a read-before-write.

## Collections with many elements

Each element inside a collection ends up as a [separate key value](../../architecture/docdb/persistence#ycql-collection-type-example) in DocDB adding per-element overhead.

If your collections are immutable, or you update the whole collection in full, consider using the `JSONB` data type. An alternative would also be to use ProtoBuf or FlatBuffers and store the serialized data in a `BLOB` column.

## Use partition_hash for large table scans

`partition_hash` function can be used for querying a subset of the data to get approximate row counts or to break down full-table operations into smaller sub-tasks that can be run in parallel. See [example usage](../../api/ycql/expr_fcall#partition-hash-function) along with a working Python script.

## Miscellaneous

## Use `TRUNCATE` to empty tables instead of `DELETE`

`TRUNCATE` deletes the database files that store the table and is very fast. While `DELETE` inserts a `delete marker` for each row in transactions and they are removed from storage when a compaction runs.
