---
title: Using Indexes
linkTitle: Using Indexes
description: Using Indexes
headcontent: Using Indexes
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: indexes-constraints-indexes-1
    parent: explore-indexes-constraints
    weight: 300
isTocNested: true
showAsideToc: true
---

The use of indexes can enhance database performance by enabling the database server to find rows faster. 

YSQL allows you to create, drop, and list indexes, as well as use various types of indexes.

## Creating Indexes

You create indexes in YSQL using the `CREATE INDEX` statement that has the following syntax:

```
CREATE INDEX index_name ON table_name(column_name);
```

*column_name* represents a list of one or more columns to be stored in the index. 

You can also create a functional-based index, in which case you would replace *column_name* with an expression. For more information, see [Using Indexes on Expressions](#using-indexes-on-expressions).

The only type of index that is currently supported by YSQL is called LSM (log-structured merge-tree). This index is based on YugabyteDB's DocDB storage and is similar in functionality to PostgreSQL's B-tree. When you create an index, you do not need to specify the type because YSQL always maps it to LSM; if you do specify the type, such as `btree`, in your `CREATE INDEX` statement, you will receive a notification about replacement of the `btree` method with `lsm`. 

You can apply sort order on the indexed columns as `ASC` (default) and `DESC`, as well as a `HASH` option on the index.

You can use the `EXPLAIN` statement to check if a query uses an index.

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees_ind (
  employee_no integer,
  name text,
  department text
);
```

```sql
INSERT INTO employees_ind VALUES 
(1221, 'John Smith', 'Marketing'),
(1222, 'Bette Davis', 'Sales'),
(1223, 'Lucille Ball', 'Operations'); 
```

The following example shows a query that finds employees working in Operations:

```sql
SELECT * FROM employees_ind
  WHERE department = 'Operations';
```

To process the preceding query, the whole `employees` table needs to be scanned. For large organizations, this might take significant amount of time.

To speed up the process, you create an index for the department column, as follows:

```sql
CREATE INDEX index_employees_ind_department
  ON employees_ind(department);
```

The following example executes the query after the index has been applied to `department` and uses the `EXPLAIN` statement to prove that the index participated in the processing of the query:

```sql
EXPLAIN SELECT * FROM employees_ind
  WHERE department = 'Operations';
```

The following is the output produced by the preceding example:

```
QUERY PLAN                        
-----------------------------------------------------------------------------------
Index Scan using index_employees_department on employees (cost=0.00..5.22 rows=10 width=68)
Index Cond: (department = 'Operations'::text)
```

For additional information, see [Create Index](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_create_index/#unique).

## Removing Indexes

You can remove one or more existing indexes using the `DROP INDEX` statement that has the following syntax:

```
DROP INDEX index_name1, index_name2, index_name3, ... ;
```

The following example shows how to remove `index_employees_ind_department` that was created in [Creating Indexes](#creating-indexes):

```sql
DROP INDEX index_employees_ind_department;
```

If you execute the same `SELECT` query with the `EXPLAIN` statement as in [Creating Indexes](#creating-indexes), the query plan will not include any information about the index. 

## Listing Indexes

YSQL inherits all the functionality of the PostgeSQL `pg_indexes` view that allows you to retrieve a list of all indexes in the database as well as detailed information about every index.

For details, see [pg_indexes](https://www.postgresql.org/docs/12/view-pg-indexes.html) in the PostgreSQL documentation.

## Using the UNIQUE Index

If you need values in some of the columns to be unique, you can specify a B-tree index as `UNIQUE`. 

When a `UNIQUE` index applied to two or more columns, the combined values in these columns cannot be duplicated in multiple rows. Note that since a `NULL` value is treated as distinct value, you can have multiple `NULL` values in a column with a `UNIQUE` index.

If a table has primary key or a `UNIQUE` constraint defined, a corresponding `UNIQUE` index is created autumatically.

The following example shows how to create a `UNIQUE` index for the `employee_no` column in the `employees` table from [Creating Indexes](#creating-indexes):

```sql
CREATE UNIQUE INDEX index_employees_no
  ON employees(employee_no);
```

After the preceding statement is executed, any attempt to insert a new employee with the same `employee_no` as one of the existing employees will result in an error.

For additional information and examples, see [Unique Index with HASH Column Ordering](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering).

## Using Indexes on Expressions

YSQL enables you to create an index based on an expression involving table columns, as per the following syntax:

```
CREATE INDEX index_name ON table_name(expression);
```

*expression* involves table columns of the *table_name* table. When the index expression is defined, this index is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

The following example uses the `employees` table from [Creating Indexes](#creating-indexes) to show how to create an index on expression that converts the department to lowercase to improve searchability:

```sql
CREATE INDEX index_employees_ind_department_lc
  ON employees_ind(LOWER(department));
```

The following `SELECT` statement with `EXPLAIN` uses `index_employees_ind_department_lc` to find a departments regardless of which case is used:

```sql
EXPLAIN SELECT * FROM employees_ind
  WHERE LOWER(department) = 'operations';
```

The following is the output produced by the preceding example:

```
QUERY PLAN                        
-----------------------------------------------------------------------------------
Index Scan using index_employees_ind_department_lc on employees_ind  (cost=0.00..5.25 rows=10 width=68)
  Index Cond: (lower(department) = 'operations'::text)
```

## Using Partial Indexes

Partial indexes allow you to improve the query performance by reducing the index size. This is done by specifying the rows of a table to be indexed. 

YSQL enables you to create an index based on an expression involving table columns, as per the following syntax:

```
CREATE INDEX index_name ON table_name(expression);
```

*expression* involves table columns of the *table_name* table. When the index expression is defined, this index is used when the expression that defines the index is included in the `WHERE` or `ORDER BY` clause in the YSQL statement.

The following example uses the `employees` table from [Creating Indexes](#creating-indexes) to show how to create an index on expression that converts the department to lowercase to improve searchability:

```sql
CREATE INDEX index_employees_ind_department_lc
  ON employees_ind(LOWER(department));
```



















Notes:

Some examples to point out:

```
yugabyte=# create table foo(a int primary key, b int);
CREATE TABLE
yugabyte=# create index on foo(b);
CREATE INDEX
yugabyte=# create index on foo using btree(b);
NOTICE:  index method "btree" was replaced with "lsm" in YugabyteDB
CREATE INDEX
```

```
yugabyte=# select * from pg_indexes where tablename='foo';
 schemaname | tablename | indexname  | tablespace |                           indexdef
------------+-----------+------------+------------+---------------------------------------------------------------
 public     | foo       | foo_pkey   |            | CREATE UNIQUE INDEX foo_pkey ON public.foo USING lsm (a HASH)
 public     | foo       | foo_b_idx  |            | CREATE INDEX foo_b_idx ON public.foo USING lsm (b HASH)
 public     | foo       | foo_b_idx1 |            | CREATE INDEX foo_b_idx1 ON public.foo USING lsm (b HASH)
(3 rows)
```

The latter is the pg_indexes table. You see there we explicitly say lsm (and explicitly call out the primary key index). (edited) 

