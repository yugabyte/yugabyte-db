---
title: Covering indexes
linkTitle: Covering indexes
description: Using covering indexes in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.14:
    identifier: covering-index-ysql
    parent: explore-indexes-constraints
    weight: 255
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../covering-index-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

A covering index is an index that includes all the columns required by a query, including columns that would typically not be a part of an index. This is done by using the INCLUDE keyword to list the columns you want to include.

A covering index is an efficient way to perform [index-only](https://wiki.postgresql.org/wiki/Index-only_scans) scans, where you don't need to scan the table, just the index, to satisfy the query.

## Syntax

```sql
CREATE INDEX columnA_columnB_index_name ON table_name(columnA, columnB) INCLUDE (columnC);
```

## Example

The following exercise demonstrates how to perform an index-only scan on an [expression (functional) index](../expression-index-ysql/), and further optimize the query performance using a covering index.

Create a cluster [locally](../../../quick-start/) or in [YugabyteDB Managed](../../../yugabyte-cloud/cloud-basics/create-clusters-free/) and connect to the cluster using [ysqlsh](../../../admin/ysqlsh/) for local clusters, or [using cloud shell](../../../yugabyte-cloud/cloud-connect/connect-cloud-shell/) for YugabyteDB Managed.

1. Create and insert some rows into a table `demo` with two columns `id` and `username`.

    ```sql
    CREATE TABLE IF NOT EXISTS demo (id bigint, username text);
    ```

    ```sql
    INSERT INTO demo SELECT n,'Number'||to_hex(n) from generate_series(1,1000) n;
    ```

1. Run a select query to fetch a row with a particular username.

    ```sql
    SELECT * FROM demo WHERE username='Number42';
    ```

    ```output
     id | username
    ----+----------
     66 | Number42
     (1 row)
    ```

1. Run another select query to show how a sequential scan runs before creating an index.

    ```sql
    EXPLAIN ANALYZE SELECT * FROM demo WHERE upper(username)='NUMBER42';
    ```

    ```output
                                                      QUERY PLAN
    ------------------------------------------------------------------------------------------------------
     Seq Scan on demo  (cost=0.00..105.00 rows=1000 width=40) (actual time=15.279..15.880 rows=1 loops=1)
       Filter: (upper(username) = 'NUMBER42'::text)
       Rows Removed by Filter: 999
     Planning Time: 0.075 ms
     Execution Time: 15.968 ms
     Peak Memory Usage: 0 kB
    (6 rows)
    ```

1. Optimize the SELECT query by creating an expression index as follows:

    ```sql
    CREATE INDEX demo_upper ON demo( (upper(username)) );
    ```

    ```sql
    EXPLAIN ANALYZE SELECT upper(username) FROM demo WHERE upper(username)='NUMBER42';
    ```

    ```output
                                                           QUERY PLAN
    -------------------------------------------------------------------------------------------------------------------
     Index Scan using demo_upper on demo  (cost=0.00..5.28 rows=10 width=32) (actual time=1.939..1.942 rows=1 loops=1)
       Index Cond: (upper(username) = 'NUMBER42'::text)
     Planning Time: 7.289 ms
     Execution Time: 2.052 ms
     Peak Memory Usage: 8 kB
    (5 rows)
    ```

    Using an expression index enables faster access to the rows requested in the query. The problem is that the query planner just takes the expression, sees that there's an index on it, and knows that you'll select the `username` column and apply a function to it. It then thinks it needs the `username` column without realizing it already has the value with the function applied. In this case, an index-only scan covering the column to the index can optimize the query performance.

1. Create a covering index by specifying the username column in the INCLUDE clause.

    For simplicity, the `username` column is used with the INCLUDE keyword to create the covering index. Generally, a covering index allows you to perform an index-only scan if the query select list matches the columns that are included in the index and the additional columns added using the INCLUDE keyword.

    Ideally, specify columns that are updated frequently in the INCLUDE clause. For other cases, it is probably faster to index all the key columns.

    ```sql
    CREATE INDEX demo_upper_covering ON demo( (upper(username))) INCLUDE (username);
    ```

    ```sql
    EXPLAIN ANALYZE SELECT upper(username) FROM demo WHERE upper(username)='NUMBER42';
    ```

    ```output
                                                                   QUERY PLAN
    ---------------------------------------------------------------------------------------------------------------------
     Index Only Scan using demo_upper_covering on demo  (cost=0.00..5.18 rows=10 width=32) (actual time=1.650..1.653 rows=1     loops=1)
       Index Cond: ((upper(username)) = 'NUMBER42'::text)
       Heap Fetches: 0
     Planning Time: 5.258 ms
     Execution Time: 1.736 ms
     Peak Memory Usage: 8 kB
    (6 rows)
    ```

## Learn more

Explore the [Benefits of an Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/) in depth with a real world example.
