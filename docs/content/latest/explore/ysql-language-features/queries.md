---
title: Queries and Joins
linkTitle: Queries and Joins
description: Queries and Joins in YSQL
headcontent: Queries and Joins in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-queries-joins
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

Intro ...

## Retrieving Data

The main purpose of `SELECT` statements is to retrieve data from specified tables. The first part of every `SELECT` statement defines columns that contain the required data, the second part points to the tables hosting these columns, and the third, optional part, lists restrictions. 

`SELECT` has the following syntax:

```sql
SELECT list FROM table_name;
```

*list* represents a column or a list of columns in a *table_name* table that is the subject of data retrieval. If you provide a list of columns, you need to use a comma separator between two columns. To select data from all the columns in the table, you can use an asterisk, as shown in the following example with a table called `employees`:

```sql
SELECT * FROM employees;
```

*list* can also contain expressions or literal values.

The `FROM` clause is evaluated before `SELECT`.

The following `SELECT` statement clauses provide flexiblity and allow you to fine-tune queries:

- The `DISTINCT` operator allows you to select distinct rows. 
- The `ORDER BY` clause lets you sort rows.
- The `WHERE` clause allows you to apply filters to rows.
- The `LIMIT` and `FETCH` clauses allow you to select a subset of rows from a table. 
- The `GROUP BY` clause allows you to divide rows into groups. 
- The `HAVING` clause lets you filter groups.
- The `INNER JOIN`, `LEFT JOIN`, `FULL OUTER JOIN`, and `CROSS JOIN` clauses let you create joins with other tables.
- `UNION`, `INTERSECT`, and `EXCEPT` allow you to perform set operations. 

#### Examples







* Aliases with `AS`

* `WHERE` clause and expressions

  * Boolean expressions - `AND` and `OR` clauses
  * Numeric expressions
  * Date expressions

* `LIMIT` and `OFFSET`

* `ORDER BY` clause

* `DISTINCT` clause

* `GROUP BY` clause

* `HAVING` clause









## Joins


### Cross Join


### Inner Join


### Left Outer Join


### Right Outer Join


### Full Outer Join


## Recursive Queries (CTEs)

* Common Table Expressions

## Hierarchical queries

Can be achieved in one of two ways:

* Using common table expressions (CTE)
* Using the `tablefunc` extension


## Miscellaneous queries

### Wildcard matches - `LIKE` clause

### Handling upper and lower case