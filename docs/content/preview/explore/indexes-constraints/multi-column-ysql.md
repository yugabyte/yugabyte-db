---
title: Muti-column indexes in YugabyteDB YSQL
headerTitle: Multi-column indexes
linkTitle: Multi-column indexes
description: Using Multi-column indexes in YSQL
headContent: Explore Multi-column indexes in YugabyteDB using YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: multi-column-index-ysql
    parent: explore-indexes-constraints
    weight: 240
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../multi-column-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../multi-column-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Multi-column indexes are also known as compound indexes using which you can create an index with multiple columns.
Multi-column indexes are similar to standard indexes as they both store a sorted table of pointers to data entries. These indexes provide faster access to data entries as it uses multiple columns to sort through data faster.

## Syntax

To add a multi-column index during table creation, you can use the following syntax:

```sql
CREATE TABLE table_name (
    col1 data_type PRIMARY KEY,
    col2 data_type,
    col3 data_type,
    col3 data_type,
    INDEX index_name (col2,col3,col4)
);
```

To add a multi-column index to an existing table, you can use the following syntax:

```sql
CREATE INDEX index_name ON table_name(col2,col3,col4);
```

{{< note >}}

The column order is very important when you create a multi-column index in YSQL because of the structure in which the index is stored. As such, these indexes have a hierarchical order from left to right. So, for the preceding syntaxes, you can perform search using the following column combinations:

```sql
(col2)
(col2,col3)
(col2,col3,col4)
```

A column combination like (col2,col4) cannot be used to search or query a table.

{{< /note >}}


## Example

{{% explore-setup-single %}}

This example uses the `employees` table from the [Northwind sample database](../../../sample-data/northwind/#install-the-northwind-sample-database).

View the contents of the `employees` table:

```sql
SELECT * FROM employees LIMIT 2;
```

```output
employee_id  | last_name | first_name |        title         | title_of_courtesy | birth_date | hire_date  |           address           |  city   | region | postal_code | country |   home_phone   | extension | photo |                                                                                                           notes                                                                                                            | reports_to |              photo_path
-------------+-----------+------------+----------------------+-------------------+------------+------------+-----------------------------+---------+--------+-------------+---------+----------------+-----------+-------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+------------+--------------------------------------
           4 | Peacock   | Margaret   | Sales Representative | Mrs.              | 1937-09-19 | 1993-05-03 | 4110 Old Redmond Rd.        | Redmond | WA     | 98052       | USA     | (206) 555-8122 | 5176      | \x    | Margaret holds a BA in English literature from Concordia College (1958) and an MA from the American Institute of Culinary Arts (1966).  She was assigned to the London office temporarily from July through November 1992. |          2 | http://accweb/emmployees/peacock.bmp
           1 | Davolio   | Nancy      | Sales Representative | Ms.               | 1948-12-08 | 1992-05-01 | 507 - 20th Ave. E.\nApt. 2A | Seattle | WA     | 98122       | USA     | (206) 555-9857 | 5467      | \x    | Education includes a BA in psychology from Colorado State University in 1970.  She also completed The Art of the Cold Call.  Nancy is a member of Toastmasters International.                                              |          2 | http://accweb/emmployees/davolio.bmp
(2 rows)

```

Suppose you want to query the subset of employees by their first and last names. The query plan using the `EXPLAIN` statement would look like the following:

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio' AND first_name='Nancy';
```

```output
                                            QUERY PLAN
---------------------------------------------------------------------------------------------------
 Seq Scan on employees  (cost=0.00..105.00 rows=1000 width=1240)
   Remote Filter: (((last_name)::text = 'Davolio'::text) AND ((first_name)::text = 'Nancy'::text))
(2 rows)
```

Without creating a multi-column index, querying the `employees` table with the `WHERE` clause scans all the 1000 rows sequentially. Creating an index limits the number of rows to be scanned for the same query.

Create a multi-column index on the columns `last_name` and `first_name` from the `employees` table as follows:

```sql
CREATE INDEX index_names ON employees(last_name, first_name);
```

Verify with the `EXPLAIN` statement that the number of rows is significantly less compared to the original query plan.

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio' AND first_name='Nancy';
```

```output
                                           QUERY PLAN
------------------------------------------------------------------------------------------------
 Index Scan using index_names on employees  (cost=0.00..5.25 rows=10 width=1240)
   Index Cond: (((last_name)::text = 'Davolio'::text) AND ((first_name)::text = 'Nancy'::text))
(2 rows)
```

With the index `index_names`, you can also search for employees by their last names as follows:

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio';
```

```output
                                    QUERY PLAN
-----------------------------------------------------------------------------------
 Index Scan using index_names on employees  (cost=0.00..16.25 rows=100 width=1240)
   Index Cond: ((last_name)::text = 'Davolio'::text)
(2 rows)
```

## Learn more

- [How To Design Distributed Indexes for Optimal Query Performance](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/)
- [The Benefit of Partial Indexes in Distributed SQL Databases](https://www.yugabyte.com/blog/the-benefit-of-partial-indexes-in-distributed-sql-databases/)