---
title: Expression Indexes
linkTitle: Expression Indexes
description: Using Expression Indexes
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: expression-index-ysql
    parent: explore-indexes-constraints
    weight: 340
aliases:
   - /latest/explore/ysql-language-features/indexes-1/
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../expression-index-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Indexes usually are created based on the columns, but the expression indexes lets you create an index based on a generic expression(function or modification of data entered) involving the table columns.

## Syntax

```ysql
CREATE INDEX index_name ON table_name(expression);
```

When the index is defined, it is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

## Example

A common use case of an expression index is to support case-insensitive text to enable efficient seachability.

For example, let's say there's a `users` table with an `email` column to store their email addresses for signing in, but the user want's to maintain authentication in a case-insensitive manner. This can be done using the `WHERE` clause as WHERE LOWER(email) = '<lower_case_email>', but storing the email address as originally entered by the user.

The following example uses the `employees` table from [Create indexes](../../indexes-constraints/overview/#create-indexes) to show how to create an index on an expression that converts the department to lowercase to improve searchability.

- Verify the query plan without creating an on the expression for the department `Operations`.

```sql
EXPLAIN SELECT * FROM employees
  WHERE LOWER(department) = 'operations';
```

```output
                         QUERY PLAN
---------------------------------------------------------------
 Seq Scan on employees  (cost=0.00..105.00 rows=1000 width=68)
   Filter: (lower(department) = 'operations'::text)
(2 rows)
```

- Create an expression index using the following command:

```sql
CREATE INDEX index_employees_department_lc
  ON employees(LOWER(department));
```

- Run the following `SELECT` statement with `EXPLAIN`  the now uses the `index_employees_department_lc` index  to find departments regardless of which case is used:

```sql
EXPLAIN SELECT * FROM employees
  WHERE LOWER(department) = 'operations';
```

```output
                          QUERY PLAN
-----------------------------------------------------------------------------------
Index Scan using index_employees_department_lc on employees  (cost=0.00..5.25 rows=10 width=68)
  Index Cond: (lower(department) = 'operations'::text)
```

## Learn more

- [Partial and expression indexes](../../json-support/jsonb-ysql/#partial-and-expression-indexes)

- [SQL Puzzle: Partial Versus Expression Indexes](https://blog.yugabyte.com/sql-puzzle-partial-versus-expression-indexes/)
