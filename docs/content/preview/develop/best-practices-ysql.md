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

## Use Application patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, you need to think through various scenarios. It would be better to choose a suitable design pattern for your application from a suite of battle-tested design paradigms like [Global database](../build-global-apps/global-database), [Multi-master](../build-global-apps/active-active-multi-master), [Standby cluster](../build-global-apps/active-active-single-master), [Duplicate Indexes](../build-global-apps/duplicate-indexes) etc. You can also mix and match these patterns as per your needs.

{{<tip>}}
For more details see, [Build Global Applications](../build-global-apps)
{{</tip>}}

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

## Delete older data quickly with partitioning

Use [Table partitioning](../../explore/ysql-language-features/advanced-features/partitions/) to split your data into multiple partitions according to date so that you can quickly delete older data by simply dropping the partition.

{{<tip>}}
For more details see, [Partition data by time](../common-patterns/timeseries/partitioning-by-time/)
{{</tip>}}

## Load balance and Failover using YugabyteDB drivers

YugabyteDB’s [Smart Driver](../../drivers-orms/smart-drivers/) provides advanced cluster-aware load-balancing capabilities that will enable your applications to send requests to multiple nodes in the cluster just by connecting to one node. You can also set a fallback hierarchy by assigning priority to specific regions and ensuring that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails

{{<tip>}}
For more details see, [Load Balancing with Smart Driver](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver)
{{</tip>}}

## Scale your application with connection pools

Setup different pools with different load balancing policies as needed for your application to scale by using popular pooling solutions such as HikariCP and Tomcat along with YugabyteDB [Smart Drivers](../../drivers-orms/smart-drivers/).

{{<tip>}}
For more details see, [Connection pooling](../../drivers-orms/smart-drivers/#connection-pooling)
{{</tip>}}

## Re-use query plans with prepared statements

Whenever possible, use [prepared statements](../../api/ysql/the-sql-language/statements/perf_prepare/) to ensure that YugabyteDB can re-use the same query plan and eliminate the need for a server to parse the query on each operation.

{{<tip>}}
For more details see, [Prepared statements in PL/pgSQL](https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3)
{{</tip>}}

## Large scans and batch jobs

Use `BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE` for batch or long-running jobs, which need a consistent snapshot of the database without interfering or being interfered with by other transactions.

{{<tip>}}
For more details see, [Long running scans](http://localhost:1313/preview/develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs)
{{</tip>}}

## Paralleling across tablets

For large/batch `SELECT`s or `DELETE`s that have to scan all tablets, you can parallelize your operation by creating queries that affect only a specific part of the tablet using the `yb_hash_code` function.

{{<tip>}}
For more details see, [Distributed Deletes/Selects](../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries)
{{</tip>}}

## TRUNCATE instead of DELETE

[TRUNCATE](../../api/ysql/the-sql-language/statements/ddl_truncate/) deletes the database files that store the table and is much faster than [DELETE](../../api/ysql/the-sql-language/statements/dml_delete/) which inserts a `delete marker` for each row in transactions that are later removed from storage during compaction runs.
