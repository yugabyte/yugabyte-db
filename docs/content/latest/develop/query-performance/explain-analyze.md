---
title: Optimizing queries with EXPLAIN and EXPLAIN ANALYZE
linkTitle: EXPLAIN and EXPLAIN ANALYZE
description: Query optimization with EXPLAIN and EXPLAIN ANALYZE
aliases:
  - /develop/query-performance/explain-analyze/
headerTitle: EXPLAIN and EXPLAIN ANALYZE
image: /images/section_icons/index/develop.png
menu:
  latest:
    identifier: explain-analyze
    parent: query-performance 
    weight: 566
isTocNested: true
showAsideToc: true
---

This section describes how to optimize queries using YSQL's `EXPLAIN` and `EXPLAIN ANALYZE` statements.

## The EXPLAIN Statement and Its Options

For each query that YSQL receives, it creates an execution plan which you can access using the `EXPLAIN` statement. The plan not only estimates the initial cost before the first row is returned, but also provides an estimate of the total execution cost for the whole result set.

You can use the `EXPLAIN` statement in conjunction with `SELECT`, `DELETE`, `INSERT`, `REPLACE`, and `UPDATE` statements. To define the `EXPLAIN` statement, use the following syntax:

```sql
EXPLAIN [ ( option [, ...] ) ] sql_statement;
```

The *option* and its values are described in the following table. The most important option is `ANALYZE`.

| Option     | Value                          | Description                                                  |
| ---------- | ------------------------------ | ------------------------------------------------------------ |
| `ANALYZE ` | `boolean`                      | Executes the *sql_statement* before receiving any run-time statistics.<br/>Since `EXPLAIN ANALYZE` discards the actual output, to perform analysis of any statement without affecting the data, you must wrap `EXPLAIN ANALYZE` in a transaction using the following syntax:<br/>`BEGIN;`<br/>`EXPLAIN ANALYZE sql_statement;`<br/>`ROLLBACK;` |
| `VERBOSE ` | `boolean`                      | Displays detailed information about the query plan. <br/>The default value is `FALSE`. |
| `COSTS `   | `boolean`                      | Provides the estimated initial and total costs of each plan node. In addition, estimates the number of rows and the width of each row in the query plan.<br/>The default value is `TRUE`. |
| `BUFFERS ` | `boolean`                      | Provides information about the most input-output intensive parts of the query. <br/>The default value is `FALSE`. <br/>You can only use this option when `ANALYZE` is set to `TRUE`. |
| `TIMING `  | `boolean`                      | Provides information about the actual startup time and the time spent in each node of the output. <br/>The default value is `TRUE`. <br/>You can only use this option when `ANALYZE` is set to `TRUE`. |
| `SUMMARY ` | `boolean`                      | Provides additional information, such as the total time after the query plan. The value of this option is `TRUE` when `ANALYZE` is set to `TRUE`. |
| `FORMAT `  | `{ TEXT | XML | JSON | YAML }` | Allows you to define the query plan output format. <br/>The default value is `TEXT`. |

### Examples

Typically, you start by creating a table in YugabyteDB and insert rows into it. 

To create a table called `employees`, execute the following:

```sql
yugabyte=# CREATE TABLE employees(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

To insert table rows, execute the following:

```sql
yugabyte=# INSERT INTO employees(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

To check the query plan for simple select, execute the following:

```sql
yugabyte=# EXPLAIN SELECT * FROM employees WHERE k1 = 1;
```

The following output displays the query execution cost estimate:

```
QUERY PLAN
----------------------------------------------------------------
Foreign Scan on employees  (cost=0.00..112.50 rows=1000 width=44)
(1 row)
```

To check the execution plan for select with a complex condition that requires filtering, execute the following:

```sql
yugabyte=# EXPLAIN SELECT * FROM employees WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```

The following output displays the cost estimate based on the filtered result:

```
QUERY PLAN
----------------------------------------------------------------
Foreign Scan on employees  (cost=0.00..125.00 rows=1000 width=44)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
(2 rows)
```

By enabling the `ANALYZE` option and wrapping it to preserve data integrity, you can trigger the query execution, as follows:

```sql
BEGIN;
yugabyte=# EXPLAIN ANALYZE SELECT * FROM employees WHERE k1 = 2 and floor(k2 + 1.5) = v1;
ROLLBACK;
```

