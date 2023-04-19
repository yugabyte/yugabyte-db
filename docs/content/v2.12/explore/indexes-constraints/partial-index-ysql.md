---
title: Partial indexes
linkTitle: Partial indexes
description: Using Partial indexes in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.12:
    identifier: partial-index-ysql
    parent: explore-indexes-constraints
    weight: 250
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../partial-index-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../partial-index-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Partial Indexes allow you to improve the query performance by reducing the index size. The smaller index size will be faster to scan and easier to maintain, thereby requiring lesser storage.
Indexing works by specifying the rows defined by a conditional expression(called the predicate of the partial index), typically in the `WHERE` clause of the table.

## Syntax

```sql
CREATE INDEX index_name ON table_name(column_list) WHERE condition;
```

The `WHERE` clause in the syntax specifies which rows need to be added to the index.

## Example

This example uses the `customers` table from the [Northwind sample database](/preview/sample-data/northwind/). Follow the steps to create a local [cluster](/preview/quick-start/) or in [Yugabyte Cloud](/preview/yugabyte-cloud/cloud-connect/), and [install](/preview/sample-data/northwind/#install-the-northwind-sample-database) the sample Northwind database.

- View the contents of the `customers` table.

```sql
SELECT * FROM customers LIMIT 3;
```

```output
customer_id |       company_name        |  contact_name  |    contact_title    |           address           |   city    | region | postal_code | country |     phone      |      fax
-------------+---------------------------+----------------+---------------------+-----------------------------+-----------+--------+-------------+---------+----------------+----------------
 FAMIA       | Familia Arquibaldo        | Aria Cruz      | Marketing Assistant | Rua Orós, 92                | Sao Paulo | SP     | 05442-030   | Brazil  | (11) 555-9857  |
 VINET       | Vins et alcools Chevalier | Paul Henriot   | Accounting Manager  | 59 rue de l'Abbaye          | Reims     |        | 51100       | France  | 26.47.15.10    | 26.47.15.11
 GOURL       | Gourmet Lanchonetes       | André Fonseca  | Sales Associate     | Av. Brasil, 442             | Campinas  | SP     | 04876-786   | Brazil  | (11) 555-9482  |
(3 rows)
```

- Let's say you want to query the subset of customers who are `Sales Managers` in the `USA`. The query plan using the `EXPLAIN` statement would look like the following:

```sql
northwind=# EXPLAIN SELECT * FROM customers where (country = 'USA' and contact_title = 'Sales Manager');
```

```output
                                     QUERY PLAN
--------------------------------------------------------------------------------------------------------
 Seq Scan on customers  (cost=0.00..105.00 rows=1000 width=738)
   Filter: (((country)::text = 'USA'::text) AND ((contact_title)::text = 'Sales Manager'::text))
(2 rows)
```

Without creating the partial index, querying the `customers` table with the `WHERE` clause will sequentially scan all the rows. Creating a partial index will limit the number of rows to be scanned for the same query.

- Create a partial index on the columns `country` and `city` from the `customers` table as follows:

```sql
northwind=# CREATE INDEX index_country ON customers(country) WHERE(contact_title = 'Sales Manager');
```

- Verify with the `EXPLAIN` statment that the number of rows will be significantly less compared to the original query plan.

```sql
northwind=# EXPLAIN SELECT * FROM customers where (country = 'USA' and contact_title = 'Sales Manager');
```

```output
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Index Scan using index_country on customers  (cost=0.00..5.00 rows=10 width=738)
   Index Cond: ((country)::text = 'USA'::text)
(2 rows)

```

## Learn more

- [SQL Puzzle: Partial Versus Expression Indexes](https://www.yugabyte.com/blog/sql-puzzle-partial-versus-expression-indexes/)

- [The Benefit of Partial Indexes in Distributed SQL Databases](https://www.yugabyte.com/blog/the-benefit-of-partial-indexes-in-distributed-sql-databases/)

- [Indexes on JSON attributes](/preview/explore/json-support/jsonb-ysql/#6-indexes-on-json-attributes)
