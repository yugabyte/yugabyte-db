---
title: Covering indexes
linkTitle: Covering indexes
description: Using Covering indexes in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: covering-index-ysql
    parent: explore-indexes-constraints
    weight: 255
aliases:
   - /latest/explore/ysql-language-features/indexes-1/
   - /latest/explore/indexes-constraints/indexes-1/
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../covering-index-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

A covering index is an index that includes all those columns required by a query using the INCLUDE keyword.

## Syntax

```ysql
CREATE INDEX columnA_columnB_index_name ON table_name(columnA, columnB) INCLUDE (columnC);
```

The following exercise demonstrates how to perform an index only scan on functional indexes, and further optimize the query performance using a covering index.

- Follow the steps to create a cluster [locally](/latest/quick-start/) or in [Yugabyte Cloud](/latest/yugabyte-cloud/cloud-connect/).

- Use the [YSQL shell](/latest/admin/ycqlsh/) for local clusters, or [Connect using Cloud shell](/latest/yugabyte-cloud/cloud-connect/connect-cloud-shell/) for Yugabyte Cloud, to create a table.

- Create and insert some rows into a table `demo` with two columns `id` and `username`.

```sql
CREATE TABLE IF NOT EXISTS demo (id bigint, username text);
```

```sql
INSERT INTO demo SELECT n,'Number'||to_hex(n) from generate_series(1,1000) n;
```

- Run a select query to fetch a row with a particular username.

```sql
SELECT * FROM demo WHERE username='Number42';
```

```output
 id | username
----+----------
 66 | Number42
(1 row)
```

- Run another select query which demonstrates a sequential scan before creating an index.

```sql
EXPLAIN ANALYZE SELECT * FROM demo WHERE upper(username)='NUMBER42';
```

```output
                                                 QUERY PLAN
------------------------------------------------------------------------------------------------------
 Seq Scan on demo  (cost=0.00..105.00 rows=1000 width=40) (actual time=15.694..16.086 rows=1 loops=1)
   Filter: (upper(username) = 'NUMBER42'::text)
   Rows Removed by Filter: 999
 Planning Time: 0.238 ms
 Execution Time: 16.255 ms
(5 rows)
```

- Optimize the SELECT query by creating a functional index as follows:

```sql
CREATE INDEX demo_upper ON demo( (upper(username)) );
```

```sql
EXPLAIN ANALYZE SELECT upper(username) FROM demo WHERE upper(username)='NUMBER42';
```

```output
                                                    QUERY PLAN
-------------------------------------------------------------------------------------------------------------------
 Index Scan using demo_upper on demo  (cost=0.00..5.28 rows=10 width=32) (actual time=2.899..2.903 rows=1 loops=1)
   Index Cond: (upper(username) = 'NUMBER42'::text)
 Planning Time: 13.392 ms
 Execution Time: 3.429 ms
(4 rows)
```

Using a function based index ([expression index](../../indexes-constraints/expression-index-ysql/)) enables faster access to the rows requested in the query. The problem is that the query planner just takes the expression, sees that there's an index on it, knows that you'll select the `username` column and apply a function on it. Then it thinks that it needs the `username` column without realizing it already has the value with the function applied. In this case, an index only scan covering the column to the index can optimize the query performance.

{{< note title="Note" >}}

For simplicity, the `username` column is used along with the INCLUDE keyword to create the covering index. Generally, a covering index allows a user to perform an index-only scan if the select list in the query matches the columns that are included in the index and additional columns for the index are specified using the INCLUDE keyword.

{{< /note >}}

- Create a covering index by including the username column along with the INCLUDE keyword.

```sql
CREATE INDEX demo_upper_covering ON demo( (upper(username))) INCLUDE (username);
```

```sql
EXPLAIN ANALYZE SELECT upper(username) FROM demo WHERE upper(username)='NUMBER42';
```

```output
                                                           QUERY PLAN
---------------------------------------------------------------------------------------------------------------------------------
 Index Only Scan using demo_upper_covering on demo  (cost=0.00..5.18 rows=10 width=32) (actual time=1.265..1.267 rows=1 loops=1)
   Index Cond: ((upper(username)) = 'NUMBER42'::text)
   Heap Fetches: 0
 Planning Time: 6.574 ms
 Execution Time: 1.342 ms
(5 rows)
```

## Learn more

Explore the [Benefits of an Index-only scan](https://blog.yugabyte.com/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/) in depth with a real world example.
