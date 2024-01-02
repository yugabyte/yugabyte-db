---
title: Expression indexes in YugabyteDB YSQL
headerTitle: Expression indexes
linkTitle: Expression indexes
description: Using expression indexes in YSQL
headContent: Explore expression indexes in YugabyteDB using YSQL
menu:
  preview:
    identifier: expression-index-ysql
    parent: explore-indexes-constraints
    weight: 250
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../expression-index-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Indexes are typically created based solely on the columns, but using an expression index (also called a function-based index) you can create an index based on a generic expression (function or modification of data entered) computed from table columns.

## Syntax

```sql
CREATE INDEX index_name ON table_name( (expression) );
```

You can omit the parentheses around the expression where the expression is a simple function call.

Once defined, the index is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

## Example

{{% explore-setup-single %}}

A common use case of an expression index is to support case-insensitive text to enable efficient searchability.

For example, suppose you have a `users` table with an `email` column to store login email addresses, and you want to maintain case-insensitive authentication. Using the WHERE clause as WHERE LOWER(email) = '<lower_case_email>' allows you to store the email address as originally entered by the user.

The following example uses the `employees` table from the Secondary indexes [example scenario](../secondary-indexes/#example-scenario-using-ysql) to show how to create an index on an expression that converts the department to lowercase to improve searchability.

1. Verify the query plan without creating an expression index for the department `Operations`.

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

1. Create an expression index using the following command:

    ```sql
    CREATE INDEX index_employees_department_lc
      ON employees(LOWER(department));
    ```

1. Run the `EXPLAIN` statement again to verify that the `index_employees_department_lc` index is used to find the department regardless of case:

    ```sql
    EXPLAIN SELECT * FROM employees
      WHERE LOWER(department) = 'operations';
    ```

    ```output
                                            QUERY PLAN
    ------------------------------------------------------------------------------------------------
     Index Scan using index_employees_department_lc on employees  (cost=0.00..5.25 rows=10 width=68)
      Index Cond: (lower(department) = 'operations'::text)
    ```

## Explore covering indexes

- Learn how [covering indexes](../../indexes-constraints/covering-index-ysql/) can optimize query performance by covering all the columns needed by a query.
- [Benefits of an Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)

## Learn more

- [Partial and expression indexes](../../json-support/jsonb-ysql/#partial-and-expression-indexes)
- [SQL Puzzle: Partial Versus Expression Indexes](https://www.yugabyte.com/blog/sql-puzzle-partial-versus-expression-indexes/)
