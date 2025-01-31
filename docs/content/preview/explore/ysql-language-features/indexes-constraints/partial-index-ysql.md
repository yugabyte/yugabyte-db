---
title: Partial indexes in YugabyteDB YSQL
headerTitle: Partial indexes
linkTitle: Partial indexes
description: Using Partial indexes in YSQL
headContent: Explore partial indexes in YugabyteDB using YSQL
menu:
  preview:
    identifier: partial-index-ysql
    parent: explore-indexes-constraints-ysql
    weight: 240
aliases:
  - /preview/explore/ysql-language-features/indexes-1/
type: docs
---

Partial indexes allow you to improve query performance by reducing the index size. A smaller index is faster to scan, easier to maintain, and requires less storage. Partial indexing works by specifying the rows defined by a conditional expression (called the predicate of the partial index), typically in the WHERE clause of the table.

## Syntax

```sql
CREATE INDEX index_name ON table_name(column_list) WHERE condition;
```

The WHERE clause specifies which rows need to be added to the index.

## Setup

The examples run on any YugabyteDB universe.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" rf="1" >}}

{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{<nav/panel name="cloud">}}{{<setup/cloud>}}{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

The example uses the `customers` table from the [Northwind sample database](../../../../sample-data/northwind/#install-the-northwind-sample-database).

View the contents of the `customers` table:

```sql
SELECT * FROM customers LIMIT 3;
```

```caddyfile{.nocopy}
 customer_id |       company_name        |  contact_name  |    contact_title    |           address           |   city    | region | postal_code | country |     phone      |      fax
-------------+---------------------------+----------------+---------------------+-----------------------------+-----------+--------+-------------+---------+----------------+----------------
 FAMIA       | Familia Arquibaldo        | Aria Cruz      | Marketing Assistant | Rua Orós, 92                | Sao Paulo | SP     | 05442-030   | Brazil  | (11) 555-9857  |
 VINET       | Vins et alcools Chevalier | Paul Henriot   | Accounting Manager  | 59 rue de l'Abbaye          | Reims     |        | 51100       | France  | 26.47.15.10    | 26.47.15.11
 GOURL       | Gourmet Lanchonetes       | André Fonseca  | Sales Associate     | Av. Brasil, 442             | Campinas  | SP     | 04876-786   | Brazil  | (11) 555-9482  |
(3 rows)
```

Suppose you want to query the subset of customers who are Sales Managers in the USA. The query plan using the EXPLAIN statement would look like the following:

```sql
northwind=# EXPLAIN SELECT * FROM customers where (country = 'USA' and contact_title = 'Sales Manager');
```

```yaml{.nocopy}
                                    QUERY PLAN
-----------------------------------------------------------------------------------------------
 Seq Scan on customers  (cost=0.00..105.00 rows=1000 width=738)
  Filter: (((country)::text = 'USA'::text) AND ((contact_title)::text = 'Sales Manager'::text))
(2 rows)
```

Without creating a partial index, querying the `customers` table using the WHERE clause scans all the rows sequentially. Creating a partial index limits the number of rows to be scanned for the same query.

Create a partial index on the columns `country` and `city` from the `customers` table as follows:

```sql
northwind=# CREATE INDEX index_country ON customers(country) WHERE(contact_title = 'Sales Manager');
```

Using the EXPLAIN statement, verify that the number of rows is significantly less compared to the original query plan.

```sql
northwind=# EXPLAIN SELECT * FROM customers where (country = 'USA' and contact_title = 'Sales Manager');
```

```yaml{.nocopy}
                                  QUERY PLAN
---------------------------------------------------------------------------------
 Index Scan using index_country on customers  (cost=0.00..5.00 rows=10 width=738)
  Index Cond: ((country)::text = 'USA'::text)
(2 rows)
```

## Learn more

- [SQL Puzzle: Partial Versus Expression Indexes](https://www.yugabyte.com/blog/sql-puzzle-partial-versus-expression-indexes/)
- [The Benefit of Partial Indexes in Distributed SQL Databases](https://www.yugabyte.com/blog/the-benefit-of-partial-indexes-in-distributed-sql-databases/)
- [Indexes on JSON attributes](../../jsonb-ysql/#indexes-on-json-attributes)
