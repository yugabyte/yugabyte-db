---
title: Best practices for Data Modeling and performance of YSQL applications
headerTitle: Best practices for Data Modeling and performance of YSQL applications
linkTitle: YSQL data modeling
description: Tips and tricks for building YSQL applications
headcontent: Tips and tricks for building YSQL applications
menu:
  stable_develop:
    identifier: data-modeling-perf
    parent: best-practices-develop
    weight: 10
type: docs
---

Designing efficient, high-performance YSQL applications requires thoughtful data modeling and an understanding of how YugabyteDB handles distributed workloads. This guide offers a collection of best practices, from leveraging colocation and indexing techniques to optimizing transactions and parallelizing queries, that can help you build scalable, globally distributed applications with low latency and high availability. Whether you're developing new applications or tuning existing ones, these tips will help you make the most of YSQL's capabilities

## Use application patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, choose a suitable design pattern for your application from a suite of battle-tested design paradigms, including [Global database](../../build-global-apps/global-database), [Multi-master](../../build-global-apps/active-active-multi-master), [Standby cluster](../../build-global-apps/active-active-single-master), [Duplicate indexes](../../build-global-apps/duplicate-indexes), [Follower reads](../../build-global-apps/follower-reads), and more. You can also combine these patterns as per your needs.

{{<lead link="../../build-global-apps">}}
For more details, see [Build global applications](../../build-global-apps).
{{</lead>}}

## Colocation

Colocated tables optimize latency and performance for data access by reducing the need for additional trips across the network for small tables. Additionally, it reduces the overhead of creating a tablet for every relation (tables, indexes, and so on) and their storage per node.

{{<lead link="../../../additional-features/colocation/">}}
For more details, see [Colocation](../../../additional-features/colocation/).
{{</lead>}}

## Faster reads with covering indexes

When a query uses an index to look up rows faster, the columns that are not present in the index are fetched from the original table. This results in additional round trips to the main table leading to increased latency.

Use [covering indexes](../../../explore/ysql-language-features/indexes-constraints/covering-index-ysql/) to store all the required columns needed for your queries in the index. Indexing converts a standard Index-Scan to an [Index-Only-Scan](https://dev.to/yugabyte/boosts-secondary-index-queries-with-index-only-scan-5e7j).

{{<lead link="https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes">}}
For more details, see [Avoid trips to the table with covering indexes](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#avoid-trips-to-the-table-with-covering-indexes).
{{</lead>}}

## Faster writes with partial indexes

A partial index is an index that is built on a subset of a table and includes only rows that satisfy the condition specified in the WHERE clause. This speeds up any writes to the table and reduces the size of the index, thereby improving speed for read queries that use the index.

{{<lead link="../../../explore/ysql-language-features/indexes-constraints/partial-index-ysql/">}}
For more details, see [Partial indexes](../../../explore/ysql-language-features/indexes-constraints/partial-index-ysql/).
{{</lead>}}

## Distinct keys with unique indexes

If you need values in some of the columns to be unique, you can specify your index as UNIQUE.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a NULL value is treated as a distinct value, you can have multiple NULL values in a column with a unique index.

{{<lead link="../../../explore/ysql-language-features/indexes-constraints/unique-index-ysql/">}}
For more details, see [Unique indexes](../../../explore/ysql-language-features/indexes-constraints/unique-index-ysql/).
{{</lead>}}

## Faster sequences with server-level caching

Sequences in databases automatically generate incrementing numbers, perfect for generating unique values like order numbers, user IDs, check numbers, and so on. They prevent multiple application instances from concurrently generating duplicate values. However, generating sequences on a database that is spread across regions could have a latency impact on your applications.

Enable [server-level caching](../../../api/ysql/exprs/sequence_functions/func_nextval/#caching-values-on-the-yb-tserver) to improve the speed of sequences, and also avoid discarding many sequence values when an application disconnects.

{{<lead link="https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76">}}
For a demo, see the YugabyteDB Friday Tech Talk on [Scaling sequences with server-level caching](https://www.youtube.com/watch?v=hs-CU3vjMQY&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=76).
{{</lead>}}

## Fast single-row transactions

Common scenarios of updating rows and fetching the results in multiple statements can lead to multiple round-trips between the application and server. In many cases, rewriting these statements as single statements using the RETURNING clause will lead to lower latencies as YugabyteDB has optimizations to make single statements faster. For example, the following statements:

```sql
SELECT v FROM txndemo WHERE k=1 FOR UPDATE;
UPDATE txndemo SET v = v + 3 WHERE k=1;
SELECT v FROM txndemo WHERE k=1;
```

can be re-written as follows:

```sql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

{{<lead link="../../../develop/learn/transactions/transactions-performance-ysql/#fast-single-row-transactions">}}
For more details, see [Fast single-row transactions](../../../develop/learn/transactions/transactions-performance-ysql/#fast-single-row-transactions).
{{</lead>}}

## Delete older data quickly with partitioning

Use [table partitioning](../../../explore/ysql-language-features/advanced-features/partitions/) to split your data into multiple partitions according to date so that you can quickly delete older data by dropping the partition.

{{<lead link="../../data-modeling/common-patterns/timeseries/partitioning-by-time/">}}
For more details, see [Partition data by time](../../data-modeling/common-patterns/timeseries/partitioning-by-time/).
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

PostgreSQL and YSQL enable you to do upserts using the INSERT ON CONFLICT clause. Similar to multi-row inserts, you can also batch multiple upserts in a single INSERT ON CONFLICT statement for better performance.

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

{{<lead link="../../../explore/ysql-language-features/data-manipulation">}}
For more information, see [Data manipulation](../../../explore/ysql-language-features/data-manipulation).
{{</lead>}}

## Re-use query plans with prepared statements

Whenever possible, use [prepared statements](../../../api/ysql/the-sql-language/statements/perf_prepare/) to ensure that YugabyteDB can re-use the same query plan and eliminate the need for a server to parse the query on each operation.

{{<warning title="Avoid explicit PREPARE or EXECUTE">}}

When using server-side pooling, avoid explicit PREPARE and EXECUTE calls and use protocol-level prepared statements instead. Explicit prepare/execute calls can make connections sticky, which prevents you from realizing the benefits of using YSQL Connection Manager{{<tags/feature/ea idea="1368">}} and server-side pooling.

Depending on your driver, you may have to set some parameters to leverage prepared statements. For example, Npgsql supports automatic preparation using the Max Auto Prepare and Auto Prepare Min Usages connection parameters, which you add to your connection string as follows:

```sh
Max Auto Prepare=100;Auto Prepare Min Usages=5;
```

Consult your driver documentation.

{{</warning>}}

{{<lead link="https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3">}}
For more details, see [Prepared statements in PL/pgSQL](https://dev.to/aws-heroes/postgresql-prepared-statements-in-pl-pgsql-jl3).
{{</lead>}}

## Large scans and batch jobs

Use BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE for batch or long-running jobs, which need a consistent snapshot of the database without interfering, or being interfered with by other transactions.

{{<lead link="../../../develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs">}}
For more details, see [Large scans and batch jobs](../../../develop/learn/transactions/transactions-performance-ysql/#large-scans-and-batch-jobs).
{{</lead>}}

## JSONB datatype

Use the [JSONB](../../../api/ysql/datatypes/type_json) datatype to model JSON data; that is, data that doesn't have a set schema but has a truly dynamic schema.

JSONB in YSQL is the same as the [JSONB datatype in PostgreSQL](https://www.postgresql.org/docs/11/datatype-json.html).

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

## Parallelizing across tablets

For large or batch SELECT or DELETE that have to scan all tablets, you can parallelize your operation by creating queries that affect only a specific part of the tablet using the `yb_hash_code` function.

{{<lead link="../../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries">}}
For more details, see [Distributed parallel queries](../../../api/ysql/exprs/func_yb_hash_code/#distributed-parallel-queries).
{{</lead>}}

## Row size limit

Big columns add up when you select full or multiple rows. For consistent latency or performance, it is recommended keeping the size under 10MB or less, and a maximum of 32MB.

## Column size limit

For consistent latency or performance, it is recommended to size columns in the 2MB range or less even though an individual column or row limit is supported till 32MB.

## TRUNCATE tables instead of DELETE

[TRUNCATE](../../../api/ysql/the-sql-language/statements/ddl_truncate/) deletes the database files that store the table data and is much faster than [DELETE](../../../api/ysql/the-sql-language/statements/dml_delete/), which inserts a _delete marker_ for each row in transactions that are later removed from storage during compaction runs.

{{<warning>}}
Currently, TRUNCATE is not transactional. Also, similar to PostgreSQL, TRUNCATE is not MVCC-safe. For more details, see [TRUNCATE](../../../api/ysql/the-sql-language/statements/ddl_truncate/).
{{</warning>}}

## Minimize the number of tablets you need

Each table and index is split into tablets and each tablet has overhead. The more tablets you need, the bigger your universe will need to be. See [Allow for tablet replica overheads](../../../best-practices-operations/administration/#allow-for-tablet-replica-overheads) for how the number of tablets affects how big your universe needs to be.

Each table and index consists of several tablets based on the [--ysql_num_shards_per_tserver](../../../reference/configuration/yb-tserver/#yb-num-shards-per-tserver) flag.

You can try one of the following methods to reduce the number of tablets:

- Use [colocation](../../../additional-features/colocation/) to group small tables into 1 tablet.
- Reduce number of tablets-per-table using the [--ysql_num_shards_per_tserver](../../../reference/configuration/yb-tserver/#yb-num-shards-per-tserver) flag.
- Use the [SPLIT INTO](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause when creating a table.
- Start with few tablets and use [automatic tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/).

Note that multiple tablets can allow work to proceed in parallel so you may not want every table to have only one tablet.
