---
title: Indexes
linkTitle: Indexes
description: Using indexes
headcontent: Indexes
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: indexes-1
    parent: explore-ysql-language-features
    weight: 250
isTocNested: true
showAsideToc: true
---

The use of indexes can enhance database performance by enabling the database server to find rows faster.

YSQL allows you to create, drop, and list indexes, as well as use indexes on expressions.

## Create indexes

You create indexes in YSQL using the `CREATE INDEX` statement that has the following syntax:

```sql
CREATE INDEX index_name ON table_name(column_list);
```

*column_list* represents a column or a comma-separated list of several columns to be stored in the index. An index created for more than one column is called a composite index.

You can also create a functional index, in which case you would replace any element of *column_list* with an expression. For more information, see [Use indexes on expressions](#use-indexes-on-expressions).

YSQL currently supports index access methods `lsm` (log-structured merge-tree) and `ybgin`. These indexes are based on YugabyteDB's DocDB storage and are similar in functionality to PostgreSQL's `btree` and `gin` indexes, respectively. The index access method can be specified with `USING <access_method_name>` after *table_name*. By default, `lsm` is chosen. For more information on `ybgin`, see [Generalized inverted index][explore-gin].

You can apply sort order on the indexed columns as `ASC` (default), `DESC`, as well as `HASH`. For examples, see [HASH and ASC examples](../../../api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering)

You can use the `EXPLAIN` statement to check if a query uses an index.

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer,
  name text,
  department text
);
```

```sql
INSERT INTO employees VALUES
(1221, 'John Smith', 'Marketing'),
(1222, 'Bette Davis', 'Sales'),
(1223, 'Lucille Ball', 'Operations');
```

The following example shows a query that finds employees working in Operations:

```sql
SELECT * FROM employees WHERE department = 'Operations';
```

To process the preceding query, the whole `employees` table needs to be scanned. For large organizations, this might take a significant amount of time.

To speed up the process, you create an index for the department column, as follows:

```sql
CREATE INDEX index_employees_department
  ON employees(department);
```

The following example executes the query after the index has been applied to `department` and uses the `EXPLAIN` statement to prove that the index participated in the processing of the query:

```sql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

The following is the output produced by the preceding example:

```output
QUERY PLAN
-----------------------------------------------------------------------------------
Index Scan using index_employees_department on employees (cost=0.00..5.22 rows=10 width=68)
Index Cond: (department = 'Operations'::text)
```

For additional information, see [Create index API](/latest/api/ysql/the-sql-language/statements/ddl_create_index/#unique).

## List indexes

YSQL inherits all the functionality of the PostgeSQL `pg_indexes` view that allows you to retrieve a list of all indexes in the database as well as detailed information about every index.

For details, see [pg_indexes](https://www.postgresql.org/docs/12/view-pg-indexes.html) in the PostgreSQL documentation.

## Use a UNIQUE index

If you need values in some of the columns to be unique, you can specify your index as `UNIQUE`.

When a `UNIQUE` index applied to two or more columns, the combined values in these columns cannot be duplicated in multiple rows. Note that since a `NULL` value is treated as distinct value, you can have multiple `NULL` values in a column with a `UNIQUE` index.

If a table has primary key or a `UNIQUE` constraint defined, a corresponding `UNIQUE` index is created autumatically.

The following example shows how to create a `UNIQUE` index for the `employee_no` column in the `employees` table from [Create indexes](#create-indexes):

```sql
CREATE UNIQUE INDEX index_employees_no
  ON employees(employee_no);
```

After the preceding statement is executed, any attempt to insert a new employee with the same `employee_no` as one of the existing employees will result in an error.

For additional information and examples, see [Unique index with HASH column ordering](/latest/api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering).

## Use indexes on expressions

YSQL enables you to create an index based on an expression involving table columns, as per the following syntax:

```ysql
CREATE INDEX index_name ON table_name(expression);
```

*expression* involves table columns of the *table_name* table. When the index expression is defined, this index is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

The following example uses the `employees` table from [Create indexes](#create-indexes) to show how to create an index on an expression that converts the department to lowercase to improve searchability:

```sql
CREATE INDEX index_employees_department_lc
  ON employees(LOWER(department));
```

The following `SELECT` statement with `EXPLAIN` uses `index_employees_department_lc` to find a departments regardless of which case is used:

```sql
EXPLAIN SELECT * FROM employees
  WHERE LOWER(department) = 'operations';
```

The following is the output produced by the preceding example:

```output
QUERY PLAN
-----------------------------------------------------------------------------------
Index Scan using index_employees_department_lc on employees  (cost=0.00..5.25 rows=10 width=68)
  Index Cond: (lower(department) = 'operations'::text)
```

## Use partial indexes

Partial indexes allow you to improve the query performance by reducing the index size. This is done by specifying the rows, typically within the `WHERE` clause, of a table to be indexed.

You can define a partial index using the following syntax:

```ysql
CREATE INDEX index_name ON table_name(column_list) WHERE condition;
```

For examples, see [Partial Indexes](/latest/api/ysql/the-sql-language/statements/ddl_create_index/#partial-indexes).

## Remove indexes

You can remove one or more existing indexes using the `DROP INDEX` statement that has the following syntax:

```ysql
DROP INDEX index_name1, index_name2, index_name3, ... ;
```

The following example shows how to remove `index_employees_department` that was created in [Create indexes](#create-indexes):

```sql
DROP INDEX index_employees_department;
```

If you execute the same `SELECT` query with the `EXPLAIN` statement as in [Create indexes](#create-indexes), the query plan will not include any information about the index.

[explore-gin]: ../gin/
