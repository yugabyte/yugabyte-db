---
title: Best practices for YSQL applications
headerTitle: Best practices
linkTitle: Best practices
description: Tips and tricks to build YSQL applications
headcontent: Tips and tricks to build YSQL applications for high performance and availability
menu:
  v2.18:
    identifier: best-practices-ysql
    parent: develop
    weight: 570
type: docs
---

{{<api-tabs>}}

## Use application patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, choose a suitable design pattern for your application from a suite of battle-tested design paradigms, including [Global database](../build-global-apps/global-database), [Multi-master](../build-global-apps/active-active-multi-master), [Standby cluster](../build-global-apps/active-active-single-master), [Duplicate indexes](../build-global-apps/duplicate-indexes), and more. You can also combine these patterns as per your needs.

{{<tip>}}
For more details, see [Build global applications](../build-global-apps).
{{</tip>}}

## Faster reads with covering indexes

When a query uses an index to look up rows faster, the columns that are not present in the index are fetched from the original table. This results in additional round trips to the main table leading to increased latency.

Use [covering indexes](../../explore/indexes-constraints/covering-index-ysql/) to store all the required columns needed for your queries in the index. Indexing converts a standard Index-Scan to an [Index-Only-Scan](https://dev.to/yugabyte/boosts-secondary-index-queries-with-index-only-scan-5e7j).

{{<tip>}}
For more details, see [Avoid trips to the table with covering indexes](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes).
{{</tip>}}

## Distinct keys with unique indexes

If you need values in some of the columns to be unique, you can specify your index as `UNIQUE`.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a NULL value is treated as a distinct value, you can have multiple NULL values in a column with a unique index.

{{<tip>}}
For more details, see [Unique indexes](../../explore/indexes-constraints/unique-index-ysql/).
{{</tip>}}

## Faster sequences with server-level caching

Sequences in databases automatically generate incrementing numbers, perfect for generating unique values like order numbers, user IDs, check numbers, and so on. They prevent multiple application instances from concurrently generating duplicate values. However, generating sequences on a database that is spread across regions could have a latency impact on your applications.

Enable [server-level caching](../../api/ysql/exprs/func_nextval/#caching-values-on-the-yb-tserver) to improve the speed of sequences, and also avoid discarding many sequence values when an application disconnects.

{{<tip>}}
For a demo, see the YugabyteDB Friday Tech Talk on [Scaling sequences with server-level caching](https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76).
{{</tip>}}

## Fast single-row transactions

Common scenarios of updating rows and fetching the results in multiple statements can lead to multiple round-trips between the application and server. In many cases, rewriting these statements as single statements using the `RETURNING` clause will lead to lower latencies as YugabyteDB has optimizations to make single statements faster. For example, the following statements:

```sql
SELECT v FROM txndemo WHERE k=1 FOR UPDATE;
UPDATE txndemo SET v = v + 3 WHERE k=1;
SELECT v FROM txndemo WHERE k=1;
```

can be re-written as follows:

```sql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

{{<tip>}}
For more details, see [Fast single-row transactions](../../develop/learn/transactions/transactions-performance-ysql/#fast-single-row-transactions).
{{</tip>}}

## Delete older data quickly with partitioning

Use [table partitioning](../../explore/ysql-language-features/advanced-features/partitions/) to split your data into multiple partitions according to date so that you can quickly delete older data by dropping the partition.

{{<tip>}}
For more details, see [Partition data by time](../common-patterns/timeseries/partitioning-by-time/).
{{</tip>}}

## Load balance and failover using smart drivers

YugabyteDB [smart drivers](../../drivers-orms/smart-drivers/) provide advanced cluster-aware load-balancing capabilities that enables your applications to send requests to multiple nodes in the cluster just by connecting to one node. You can also set a fallback hierarchy by assigning priority to specific regions and ensuring that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails.

{{<tip>}}
For more details, see [Load balancing with smart drivers](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver).
{{</tip>}}

## Scale your application with connection pools

Set up different pools with different load balancing policies as needed for your application to scale by using popular pooling solutions such as HikariCP and Tomcat along with YugabyteDB [smart drivers](../../drivers-orms/smart-drivers/).

{{<tip>}}
For more details, see [Connection pooling](../../drivers-orms/smart-drivers/#connection-pooling).
{{</tip>}}

## Re-use query plans with prepared statements

Whenever possible, use [prepared statements](../../api/ysql/the-sql-language/statements/perf_prepare/) to ensure that YugabyteDB can re-use the same query plan and eliminate the need for a server to parse the query on each operation.

{{<tip>}}
For more details, see [Prepared statements in PL/pgSQL](https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3).
{{</tip>}}

## Large scans and batch jobs

Use `BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE` for batch or long-running jobs, which need a consistent snapshot of the database without interfering, or being interfered with by other transactions.

{{<tip>}}
For more details, see [Large scans and batch jobs](../../develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs).
{{</tip>}}

## Paralleling across tablets

For large or batch `SELECT`s or `DELETE`s that have to scan all tablets, you can parallelize your operation by creating queries that affect only a specific part of the tablet using the `yb_hash_code` function.

{{<tip>}}
For more details, see [Distributed parallel queries](../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries).
{{</tip>}}

## TRUNCATE tables instead of DELETE

[TRUNCATE](../../api/ysql/the-sql-language/statements/ddl_truncate/) deletes the database files that store the table data and is much faster than [DELETE](../../api/ysql/the-sql-language/statements/dml_delete/), which inserts a _delete marker_ for each row in transactions that are later removed from storage during compaction runs.

{{<warning>}}
Currently, TRUNCATE is not transactional. Also, similar to PostgreSQL, TRUNCATE is not MVCC-safe. For more details, see [TRUNCATE](../../api/ysql/the-sql-language/statements/ddl_truncate#truncate).
{{</warning>}}
