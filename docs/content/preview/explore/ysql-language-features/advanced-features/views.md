---
title: Views
linkTitle: Views
description: Views in YSQL
menu:
  preview:
    identifier: advanced-features-views
    parent: advanced-features
    weight: 230
aliases:
  - /preview/explore/ysql-language-features/views/
type: docs
---

This document describes how to create, use, and manage views in YSQL.

Regular views allow you to present data in YugabyteDB tables by using a different variety of named queries. In essence, a view is a proxy for a complex query to which you assign a name. In YSQL, views do not store data. However, YSQL also supports materialized views which _do_ store the results of the query.

{{% explore-setup-single %}}

## Create views

You create views based on the following syntax:

```output.sql
CREATE VIEW view_name AS query_definition;
```

*query_definition* can be a basic `SELECT` statement or a `SELECT` statement with joins.

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  address text,
  department text
);
```

```sql
INSERT INTO employees VALUES
  (1221, 'John Smith', '1 Main Street', 'Marketing'),
  (1222, 'Bette Davis', '2 Second Avenue', 'Sales'),
  (1223, 'Lucille Ball', '3 Third Avenue', 'Operations'),
  (1224, 'John Zimmerman', '4 Fourth Avenue', 'Sales');
```

The following very simplified example creates a view based on only one table and selects two of its columns:

```sql
CREATE VIEW employees_view AS
  SELECT employee_no, name FROM employees;
```

The following example shows how to query `employees_view`:

```sql
SELECT * FROM employees_view;
```

The preceding query produces the following output:

```output
employee_no | name
------------+---------------------------
1223        | Lucille Ball
1224        | John Zimmerman
1221        | John Smith
1222        | Bette Davis
```

If you create a view based on multiple tables with joins, using this view in your queries would significantly simplify the process.

## Modify views

You can modify the query based on which a view was created by combining the `CREATE VIEW` statement with `OR REPLACE`, as demonstrated by the following syntax:

```output.sql
CREATE OR REPLACE VIEW view_name AS query_definition;
```

YSQL does not allow you to remove existing columns from the view (your query has to produce the same columns that were produced when you created the view), but it allows you to append more columns, as shown in the following example:

```sql
CREATE OR REPLACE VIEW employees_view AS
  SELECT employee_no, name, department FROM employees;
```

The following example shows how to query `employees_view` to verify that the `department` column was appended to the end of the columns list:

```sql
SELECT * FROM employees_view;
```

The preceding query produces the following output:

```output
 employee_no | name             | department
-------------+------------------+----------------
 1223        | Lucille Ball     | Operations
 1224        | John Zimmerman   | Sales
 1221        | John Smith       | Marketing
 1222        | Bette Davis      | Sales
```

## Delete views

You can remove (drop) an existing view by using the `DROP VIEW` statement, as demonstrated by the following syntax:

```output.sql
DROP VIEW [ IF EXISTS ] view_name;
```

It is recommended to add the `IF EXISTS` option to the `DROP VIEW` statement: if you omit this option and attempt to drop the view, an error will occur.

The following example shows how to remove a view from the database:

```sql
DROP VIEW IF EXISTS employees_view;
```

You can also remove more than one view by providing a comma-separated list of view names.

## Use updatable views

Some YSQL views are updatable. The defining query of such views (1) must have only one entry (either a table or another updatable view) in its `FROM` clause; and (2) cannot contain `DISTINCT`, `GROUP BY`, `HAVING`, `EXCEPT`, `INTERSECT`, or `LIMIT` clauses at the top level. In addition, the view's selection list cannot contain  window functions, set-returning or aggregate functions.

The following example shows how to update the `employees` table with a new row via the `employees_view` defined in [Creating Views](#creating-views):

```sql
INSERT INTO employees_view (employee_no, name)
  VALUES (1227, 'Lee Bo');
```

If you select everything from the `employees` table by executing `SELECT * FROM employees;` , you should expect the following output:

```output
 employee_no | name             | address        | department
-------------+------------------+----------------+--------------
 1227        | Lee Bo           |                |
 1223        | Lucille Ball     | 3 Third Avenue | Operations
 1224        | John Zimmerman   | 4 Fourth Avenue| Sales
 1221        | John Smith       | 1 Main Street  | Marketing
 1222        | Bette Davis      | 2 Second Avenue| Sales
```

Executing `INSERT`, `UPDATE`, or `DELETE` on an updatable view converts that statement into the corresponding statement of the base table.

If the defining query of a view contains a `WHERE` clause, you are allowed to modify the rows that are not visible via the view.

An updatable view can contain a combination of updatable and non-updatable columns. An attempt to update the latter results in an error.

To be able to update a view, you need to have the required privilege on this view (but not necessarily on the base table).

The following example shows how use the `employees_view` to delete the row that has been added to the  `employees` table:

```sql
DELETE FROM employees_view
  WHERE employee_no = 1227;
```

## Materialized views

Materialized views are relations that persist the results of a query. They can be created using the `CREATE MATERIALIZED VIEW` command, and their contents can be updated using the `REFRESH MATERIALIZED VIEW` command.

The following very simplified example creates a materialized view based on only one table and selects two of its columns:

```sql
CREATE MATERIALIZED VIEW employees_mview AS
  SELECT employee_no, name FROM employees;
```

The following example shows how to query `employees_mview`:

```sql
SELECT * FROM employees_mview;
```

The preceding query produces the following output:

```output
employee_no | name
------------+---------------------------
1223        | Lucille Ball
1224        | John Zimmerman
1221        | John Smith
1222        | Bette Davis
```

```sql
INSERT INTO employees VALUES
  (1225, 'Jane Doe', '4 Fifth Street', 'Accounting');
```

After inserting values into the base relation (`employees`), we will have to `REFRESH` the materialized view to update its contents.

```sql
REFRESH MATERIALIZED VIEW employees_mview;
```

```sql
SELECT * FROM employees_mview;
```

The preceding query produces the following output:

```output
employee_no | name
------------+---------------------------
1223        | Lucille Ball
1224        | John Zimmerman
1221        | John Smith
1222        | Bette Davis
1225        | Jane Doe
```

## Read more

For detailed documentation on materialized views, refer to the following topics:

- [CREATE MATERIALIZED VIEW](../../../../api/ysql/the-sql-language/statements/ddl_create_matview/)
- [REFRESH MATERIALIZED VIEW](../../../../api/ysql/the-sql-language/statements/ddl_refresh_matview/)
- [DROP MATERIALIZED VIEW](../../../../api/ysql/the-sql-language/statements/ddl_drop_matview/)
