---
title: Expression indexes in YugabyteDB YSQL
headerTitle: Expression indexes
linkTitle: Expression indexes
description: Using expression indexes in YSQL
headContent: Explore expression indexes in YugabyteDB using YSQL
menu:
  stable:
    identifier: expression-index-ysql
    parent: explore-indexes-constraints-ysql
    weight: 250
type: docs
---

Indexes are typically created based solely on the columns, but using an expression index (also called a function-based index) you can create an index based on a generic expression (function or modification of data entered) computed from table columns.

## Syntax

```sql
CREATE INDEX index_name ON table_name( (expression) );
```

You can omit the parentheses around the expression where the expression is a simple function call.

Once defined, the index is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

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

A common use case of an expression index is to support case-insensitive text to enable efficient searchability.

For example, suppose you have a `users` table with an `email` column to store login email addresses, and you want to maintain case-insensitive authentication. Using the WHERE clause as `WHERE LOWER(email) = '<lower_case_email>'` allows you to store the email address as originally entered by the user.

The following example uses the `employees` table from the Secondary indexes [example scenario](../secondary-indexes-ysql/#example) to show how to create an index on an expression that converts the department to lowercase to improve searchability.

1. Verify the query plan without creating an expression index for the department `Operations`.

    ```sql
    EXPLAIN SELECT * FROM employees WHERE LOWER(department) = 'operations';
    ```

    ```yaml{.nocopy}
                            QUERY PLAN
    ---------------------------------------------------------------
     Seq Scan on employees  (cost=0.00..105.00 rows=1000 width=68)
      Filter: (lower(department) = 'operations'::text)
    ```

1. Create an expression index using the following command:

    ```sql
    CREATE INDEX index_employees_department_lc ON employees(LOWER(department));
    ```

1. Run the `EXPLAIN` statement again to verify that the `index_employees_department_lc` index is used to find the department regardless of case:

    ```sql
    EXPLAIN SELECT * FROM employees WHERE LOWER(department) = 'operations';
    ```

    ```yaml{.nocopy}
                                            QUERY PLAN
    ------------------------------------------------------------------------------------------------
     Index Scan using index_employees_department_lc on employees  (cost=0.00..5.25 rows=10 width=68)
      Index Cond: (lower(department) = 'operations'::text)
    ```

## Explore covering indexes

- Learn how [covering indexes](../covering-index-ysql/) can optimize query performance by covering all the columns needed by a query.
- [Benefits of an Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)

## Learn more

- [Partial and expression indexes](../../jsonb-ysql/#partial-and-expression-indexes)
- [SQL Puzzle: Partial Versus Expression Indexes](https://www.yugabyte.com/blog/sql-puzzle-partial-versus-expression-indexes/)
- [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#expression-indexes)
