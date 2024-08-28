---
title: Best practices for YSQL applications
headerTitle: Best practices
linkTitle: Best practices
description: Tips and tricks to build YSQL applications
headcontent: Tips and tricks to build YSQL applications for high performance and availability
menu:
  preview:
    identifier: best-practices-ysql
    parent: develop
    weight: 570
type: docs
---

{{<api-tabs>}}

## Use application patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, choose a suitable design pattern for your application from a suite of battle-tested design paradigms, including [Global database](../build-global-apps/global-database), [Multi-master](../build-global-apps/active-active-multi-master), [Standby cluster](../build-global-apps/active-active-single-master), [Duplicate indexes](../build-global-apps/duplicate-indexes), [Follower reads](../build-global-apps/follower-reads), and more. You can also combine these patterns as per your needs.

{{<lead link="../build-global-apps">}}
For more details, see [Build global applications](../build-global-apps).
{{</lead>}}

## Colocation

Colocated tables optimize latency and performance for data access by reducing the need for additional trips across the network for small tables. Additionally, it reduces the overhead of creating a tablet for every relation (tables, indexes, and so on) and their storage per node.

{{<lead link="../../explore/colocation/">}}
For more details, see [Colocation](../../explore/colocation/).
{{</lead>}}

## Faster reads with covering indexes

When a query uses an index to look up rows faster, the columns that are not present in the index are fetched from the original table. This results in additional round trips to the main table leading to increased latency.

Use [covering indexes](../../explore/ysql-language-features/indexes-constraints/covering-index-ysql/) to store all the required columns needed for your queries in the index. Indexing converts a standard Index-Scan to an [Index-Only-Scan](https://dev.to/yugabyte/boosts-secondary-index-queries-with-index-only-scan-5e7j).

{{<lead link="https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes">}}
For more details, see [Avoid trips to the table with covering indexes](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes).
{{</lead>}}

## Faster writes with partial indexes

A partial index is an index that is built on a subset of a table and includes only rows that satisfy the condition specified in the `WHERE` clause. This speeds up any writes to the table and reduces the size of the index, thereby improving speed for read queries that use the index.

{{<lead link="../../explore/ysql-language-features/indexes-constraints/partial-index-ysql/">}}
For more details, see [Partial indexes](../../explore/ysql-language-features/indexes-constraints/partial-index-ysql/).
{{</lead>}}

## Distinct keys with unique indexes

If you need values in some of the columns to be unique, you can specify your index as `UNIQUE`.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a NULL value is treated as a distinct value, you can have multiple NULL values in a column with a unique index.

{{<lead link="../../explore/ysql-language-features/indexes-constraints/unique-index-ysql/">}}
For more details, see [Unique indexes](../../explore/ysql-language-features/indexes-constraints/unique-index-ysql/).
{{</lead>}}

## Faster sequences with server-level caching

Sequences in databases automatically generate incrementing numbers, perfect for generating unique values like order numbers, user IDs, check numbers, and so on. They prevent multiple application instances from concurrently generating duplicate values. However, generating sequences on a database that is spread across regions could have a latency impact on your applications.

Enable [server-level caching](../../api/ysql/exprs/func_nextval/#caching-values-on-the-yb-tserver) to improve the speed of sequences, and also avoid discarding many sequence values when an application disconnects.

{{<lead link="https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76">}}
For a demo, see the YugabyteDB Friday Tech Talk on [Scaling sequences with server-level caching](https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76).
{{</lead>}}

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

{{<lead link="../../develop/learn/transactions/transactions-performance-ysql/#fast-single-row-transactions">}}
For more details, see [Fast single-row transactions](../../develop/learn/transactions/transactions-performance-ysql/#fast-single-row-transactions).
{{</lead>}}

## Delete older data quickly with partitioning

Use [table partitioning](../../explore/ysql-language-features/advanced-features/partitions/) to split your data into multiple partitions according to date so that you can quickly delete older data by dropping the partition.

{{<lead link="../common-patterns/timeseries/partitioning-by-time/">}}
For more details, see [Partition data by time](../common-patterns/timeseries/partitioning-by-time/).
{{</lead>}}

## Use the right data types for partition keys

In general, integer, arbitrary precision number, character string (not very long ones), and timestamp types are safe choices for comparisons.

Avoid the following:

- Floating point number data types - because they are stored as binary float format that cannot represent most of the decimal values precisely, values that are supposedly the same may not be treated as a match because of possible multiple internal representations.

- Date, time, and similar timestamp component types if they may be compared with values from a different timezone or different day of the year, or when either value comes from a country or region that observes or ever observed daylight savings time.

## Use multi row inserts wherever possible

If you're inserting multiple rows, it's faster to batch them together whenever possible. You can start with 128 rows per batch
and test different batch sizes to find the sweet spot.

Don't use multiple statements:

```postgresql
INSERT INTO users(name,surname) VALUES ('bill', 'jane');
INSERT INTO users(name,surname) VALUES ('billy', 'bob');
INSERT INTO users(name,surname) VALUES ('joey', 'does');
```

Instead, group values into a single statement as follows:

```postgresql
INSERT INTO users(name,surname) VALUES ('bill', 'jane'), ('billy', 'bob'), ('joe', 'does');
```

## UPSERT multiple rows wherever possible

PostgreSQL and YSQL enable you to do upserts using the `INSERT ON CONFLICT` clause. Similar to multi-row inserts, you can also batch multiple upserts in a single `INSERT ON CONFLICT` statement for better performance.

In case the row already exists, you can access the existing values using `EXCLUDED.<column_name>` in the query.

The following example creates a table to track the quantity of products, and increments rows in batches:

```postgresql
CREATE TABLE products
  (
     name     TEXT PRIMARY KEY,
     quantity BIGINT DEFAULT 0
  );
---
INSERT INTO products(name, quantity)
VALUES
  ('apples', 1),
  ('oranges', 5) ON CONFLICT(name) DO UPDATE
SET
  quantity = products.quantity + excluded.quantity;
---
INSERT INTO products(name, quantity)
VALUES
  ('apples', 1),
  ('oranges', 5) ON CONFLICT(name) DO UPDATE
SET
  quantity = products.quantity + excluded.quantity;
---
SELECT * FROM products;
  name   | quantity
---------+----------
 apples  |        2
 oranges |       10
(2 rows)
```

{{<lead link="../../explore/ysql-language-features/data-manipulation">}}
For more information, see [Data manipulation](../../explore/ysql-language-features/data-manipulation).
{{</lead>}}

## Load balance and failover using smart drivers

YugabyteDB [smart drivers](../../drivers-orms/smart-drivers/) provide advanced cluster-aware load-balancing capabilities that enables your applications to send requests to multiple nodes in the cluster just by connecting to one node. You can also set a fallback hierarchy by assigning priority to specific regions and ensuring that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails.

{{<lead link="https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver">}}
For more information, see [Load balancing with smart drivers](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver).
{{</lead>}}

## Scale your application with connection pools

Set up different pools with different load balancing policies as needed for your application to scale by using popular pooling solutions such as HikariCP and Tomcat along with YugabyteDB [smart drivers](../../drivers-orms/smart-drivers/).

{{<lead link="../../drivers-orms/smart-drivers/#connection-pooling">}}
For more information, see [Connection pooling](../../drivers-orms/smart-drivers/#connection-pooling).
{{</lead>}}

## Use YSQL Connection Manager

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager {{<badge/tp>}}, which provides the same connection pooling advantages as other external pooling solutions, but without many of their limitations. As the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections.

{{<tip>}}
For more information, refer to the following:

- [YSQL Connection Manager](../../explore/going-beyond-sql/connection-mgr-ysql/)
- [Built-in Connection Manager Turns Key PostgreSQL Weakness into a Strength](https://www.yugabyte.com/blog/connection-pooling-management/)
{{</tip>}}

## Re-use query plans with prepared statements

Whenever possible, use [prepared statements](../../api/ysql/the-sql-language/statements/perf_prepare/) to ensure that YugabyteDB can re-use the same query plan and eliminate the need for a server to parse the query on each operation.

{{<lead link="https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3">}}
For more details, see [Prepared statements in PL/pgSQL](https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3).
{{</lead>}}

## Large scans and batch jobs

Use `BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE` for batch or long-running jobs, which need a consistent snapshot of the database without interfering, or being interfered with by other transactions.

{{<lead link="../../develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs">}}
For more details, see [Large scans and batch jobs](../../develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs).
{{</lead>}}

## JSONB datatype

Use the [JSONB](../../api/ysql/datatypes/type_json) datatype to model JSON data; that is, data that doesn't have a set schema but has a truly dynamic schema.

JSONB in YSQL is the same as the [JSONB](https://www.postgresql.org/docs/11/datatype-json.html) datatype in PostgreSQL.

You can use JSONB to group less interesting or less frequently accessed columns of a table.

YSQL also supports JSONB expression indexes, which can be used to speed up data retrieval that would otherwise require scanning the JSON entries.

{{< note title="Use JSONB columns only when necessary" >}}

- A good schema design is to only use JSONB for truly dynamic schema. That is, don't create a "data JSONB" column where you put everything; instead, create a JSONB column for dynamic data, and use regular columns for the other data.
- JSONB columns are slower to read/write compared to normal columns.
- JSONB values take more space because they need to store keys in strings, and maintaining data consistency is harder, requiring more complex queries to get/set JSONB values.
- JSONB is a good fit when writes are done as a whole document with a per-row hierarchical structure. If there are arrays, the choice is not JSONB vs. column, but vs additional relational tables.
- For reads, JSONB is a good fit if you read the whole document and the searched expression is indexed.
- When reading one attribute frequently, it's better to move it to a column as it can be included in an index for an `Index Only Scan`.

{{< /note >}}

## Paralleling across tablets

For large or batch `SELECT`s or `DELETE`s that have to scan all tablets, you can parallelize your operation by creating queries that affect only a specific part of the tablet using the `yb_hash_code` function.

{{<lead link="../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries">}}
For more details, see [Distributed parallel queries](../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries).
{{</lead>}}

## Single availability zone (AZ) deployments

In single AZ deployments, you need to set the [yb-tserver](../../reference/configuration/yb-tserver) flag `--durable_wal_write=true` to not lose data if the whole data center goes down (For example, power failure).

## Row size limit

Big columns add up when you select full or multiple rows. For consistent latency or performance, it is recommended keeping the size under 10MB or less, and a maximum of 32MB.

## Column size limit

For consistent latency or performance, it is recommended to size columns in the 2MB range or less even though an individual column or row limit is supported till `32MB`.

## TRUNCATE tables instead of DELETE

[TRUNCATE](../../api/ysql/the-sql-language/statements/ddl_truncate/) deletes the database files that store the table data and is much faster than [DELETE](../../api/ysql/the-sql-language/statements/dml_delete/), which inserts a _delete marker_ for each row in transactions that are later removed from storage during compaction runs.

{{<warning>}}
Currently, TRUNCATE is not transactional. Also, similar to PostgreSQL, TRUNCATE is not MVCC-safe. For more details, see [TRUNCATE](../../api/ysql/the-sql-language/statements/ddl_truncate/).
{{</warning>}}

## Minimize the number of tablets you need

Each table and index is split into tablets and each tablet has overhead. The more tablets you need, the bigger your universe will need to be. See [allowing for tablet replica overheads](#allowing-for-tablet-replica-overheads) for how the number of tablets affects how big your universe needs to be.

Each table and index consists of several tablets based on the [`--ysql_num_shards_per_tserver`](../../reference/configuration/yb-tserver/#yb-num-shards-per-tserver) flag.

You can try one of the following methods to reduce the number of tablets:

- Use [colocation](../../explore/colocation/) to group small tables into 1 tablet.
- Reduce number of tablets-per-table using [`--ysql_num_shards_per_tserver`](../../reference/configuration/yb-tserver/#yb-num-shards-per-tserver) flag.
- Use [`SPLIT INTO`](../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause when creating a table.
- Start with few tablets and use [automatic tablet splitting](../../architecture/docdb-sharding/tablet-splitting/).

Note that multiple tablets can allow work to proceed in parallel so you may not want every table to have only one tablet.

## Allowing for tablet replica overheads

For a universe with [RF3](../../architecture/key-concepts/#replication-factor-rf), 1000 tablets imply 3000 tablet replicas. If the universe has three nodes, then each node has on average 1000 tablet replicas. A six node universe would have on average 500 tablet replicas per-node and so on.

Each 1000 tablet replicas on a node impose an overhead of 0.4 vCPUs for Raft heartbeats (assuming a 0.5 second heartbeat interval), 800 MiB of memory, and 128 GB of storage space for write-ahead logs (WALs).

The overhead is proportional to the number of tablet replicas so 500 tablet replicas would need half as much.

Additional memory will be required for supporting caches and the like if the tablets are being actively used. We recommend provisioning an extra 6200 MiB of memory for each 1000 tablet replicas on a node to handle these cases; that is, a TServer should have 7000 MiB of RAM allocated to it for each 1000 tablet replicas it may be expected to support.

Manually provisioning the amount of memory each TServer uses can be done using the [`--memory_limit_hard_bytes`](../../reference/configuration/yb-tserver/#memory-limit-hard-bytes) or [`--default_memory_limit_to_ram_ratio`](../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio) flags.  Manually provisioning is a bit tricky as you need to take in account how much memory the kernel needs as well as Postgres and any master process that is going to be colocated with the TServer.

Accordingly, it is recommended that you instead use the new [`--use_memory_defaults_optimized_for_ysql`](../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag, which gives you good memory division settings for using YSQL optimized for your node's size.  Consult the table there showing node RAM versus maximum tablet replicas to see how big of a node you will need based on how many tablet replicas per server you want supported.

## Settings for CI and CD integration tests

You can set certain flags to increase performance using YugabyteDB in CI and CD automated test scenarios as follows:

- Point the flags `--fs_data_dirs`, and `--fs_wal_dirs` to a RAMDisk directory to make DML, DDL, cluster creation, and cluster deletion faster, ensuring that data is not written to disk.
- Set the flag `--yb_num_shards_per_tserver=1`. Reducing the number of shards lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Use colocated databases in YSQL. Colocation lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Set the flag `--replication_factor=1` for test scenarios, as keeping the data three way replicated (default) is not necessary. Reducing that to 1 reduces space usage and increases performance.
- Use `TRUNCATE table1,table2,table3..tablen;` instead of `CREATE TABLE`, and `DROP TABLE` between test cases.
