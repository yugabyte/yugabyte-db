---
title: Primary keys in YugabyteDB YSQL
headerTitle: Primary keys
linkTitle: Primary keys
description: Defining Primary key constraint in YSQL
headContent: Explore primary keys in YugabyteDB using YSQL
menu:
  preview:
    identifier: primary-key-ysql
    parent: explore-indexes-constraints-ysql
    weight: 200
aliases:
  - /preview/explore/ysql-language-features/constraints/
  - /preview/explore/indexes-constraints/constraints/
type: docs
---

The Primary Key constraint is a means to uniquely identify a specific row in a table via one or more columns. To define a primary key, you create a constraint that is, functionally, a [unique index](../unique-index-ysql/#using-a-unique-index) applied to the table columns. It is crucial to choose and design the primary key of the table for several reasons.

- **Uniqueness**: The Primary key is a column or a set of columns that act as the unique identifier for the rows of the table. This is essential to identify a row uniquely across the different nodes in the cluster.
- **Data distribution**: In YugabyteDB, data is distributed based on the primary key. In [Hash sharding](../../../../explore/going-beyond-sql/data-sharding#hash-sharding), the data is distributed based on the Hash of the Primary key and in [range sharding](../../../../explore/going-beyond-sql/data-sharding#range-sharding), it is based on the actual value of the primary key.
- **Data ordering**: The table data is internally ordered based on the primary key of the table. In [Hash sharding](../../../../explore/going-beyond-sql/data-sharding#hash-sharding), the data is ordered based on the Hash of the Primary key and in [range sharding](../../../../explore/going-beyond-sql/data-sharding#range-sharding), it is ordered on the actual value of the primary key.

# Definition of the primary key

Primary keys can be defined along with the table definition using the `PRIMARY KEY (columns)` clause. They can also be added after the creation of the table using the [ALTER TABLE](../../../../explore/ysql-language-features/indexes-constraints/primary-key-ysql/#alter-table) statement, but this is not advisable as adding a primary key after data has been loaded could be an expensive operation to re-order and re-distribute the data.

{{<warning>}}
In the absence of an explicit primary key, YugabyteDB automatically inserts an internal **row_id** to be used as the primary key. This **row_id** is not accessible by users.
{{</warning>}}

In [Hash sharding](../../../../explore/going-beyond-sql/data-sharding#hash-sharding) the primary key definition is of the format,

```sql{.nocopy}
PRIMARY KEY ((columns),    columns)
--           [SHARDING]    [CLUSTERING]
```

The first set of columns typically referred to as sharding columns is used for the distribution of the rows and the second set of columns, referred to as Clustering columns defines the ordering of rows with the same sharding values.

In range shading, the primary key is of the format,

```sql{.nocopy}
PRIMARY KEY (columns)
--          [CLUSTERING]
```

The order of the keys matters a lot in range sharding, as the data is distributed and ordered based on the first column and for the rows with the same first column, the rows are ordered on the second column and so on.

## Syntax and examples

{{<note>}}
To explain the behavior of the queries, the examples use **explain (analyze, dist, costs off)**. In practice, you do not need to do this unless you are trying to optimize performance. For more details, see [Analyze queries](../../../../explore/query-1-performance/explain-analyze).
{{</note>}}

{{% explore-setup-single %}}

### Primary key for a single column

Most commonly, the primary key is added to the table when the table is created, as demonstrated by the following syntax:

```sql
CREATE TABLE (
  column1 data_type PRIMARY KEY,
  column2 data_type,
  column3 data_type,
  …
);
```

The following example creates the `employees` table with `employee_no` as the primary key, which uniquely identifies an employee.

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text
);
```

### Primary key over multiple columns

The following syntax can be used to define a primary key for more than one column:

```sql
CREATE TABLE (
  column1 data_type,
  column2 data_type,
  column3 data_type,
  …
  PRIMARY KEY (column1, column2)
);
```

The following example creates the `employees` table in which the primary key is a combination of `employee_no` and `name` columns:

```sql
CREATE TABLE employees (
  employee_no integer,
  name text,
  department text,
  PRIMARY KEY (employee_no, name)
);
```

### CONSTRAINT

YSQL assigns a default name in the format `tablename_pkey` to the primary key constraint. For example, the default name is `employees_pkey` for the `employees` table. If you need a different name, you can specify it using the `CONSTRAINT` clause, as per the following syntax:

```sql
CONSTRAINT constraint_name PRIMARY KEY(column1, column2, ...);
```

The following example demonstrates the use of `CONSTRAINT` to change the default name.

```sql
CONSTRAINT employee_no_pkey PRIMARY KEY(employee_no);
```

### ALTER TABLE

Use the `ALTER TABLE` statement to create a primary key on an existing table with following syntax:

```sql
ALTER TABLE table_name ADD PRIMARY KEY (column1, column2);
```

The following example creates the `employees` table first and then alters it to add a primary key on the `employee_no` column:

```sql
CREATE TABLE employees (
  employee_no integer,
  name text,
  department text
);

ALTER TABLE employees ADD PRIMARY KEY (employee_no);
```

The following example allows you to add an auto-incremented primary key to a new column on an existing table using the `GENERATED ALWAYS AS IDENTITY` property.

```sql
CREATE TABLE sample(c1 text, c2 text);
ALTER TABLE sample ADD COLUMN ID INTEGER;
ALTER TABLE sample ALTER COLUMN ID set NOT NULL;
ALTER TABLE sample ALTER COLUMN ID ADD GENERATED ALWAYS AS IDENTITY;
ALTER TABLE sample ADD CONSTRAINT sample_id_pk PRIMARY KEY (ID);
```

Insert values into the `sample` table and check the contents.

```sql
yb_demo=# INSERT INTO sample(id, c1, c2)
             VALUES (1, 'cat'  , 'kitten'),
                    (2, 'dog'  , 'puppy'),
                    (3, 'duck' , 'duckling');

yb_demo=# SELECT * FROM sample;
```

```output
   c1   |    c2     | id
--------+-----------+----
 cat    | kitten    |  1
 dog    | puppy     |  2
 duck   | duckling  |  3
(3 rows)
```

Trying to insert values for `id` into the `sample` results in the following error as the auto-increment property is now set.

```sql
yb_demo=# INSERT INTO sample(id, c1, c2)
             VALUES (4, 'cow' , 'calf'),
                    (5, 'lion', 'cub');
```

```output
ERROR:  cannot insert into column "id"
DETAIL:  Column "id" is an identity column defined as GENERATED ALWAYS.
HINT:  Use OVERRIDING SYSTEM VALUE to override.
```

<!-- ## Primary Key recommended practices in future-->

<!-- Add information here  -->

## Learn more

- [Primary Key](../../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key)
- [Table with Primary Key](../../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-primary-key)
- [Natural versus Surrogate Primary Keys in a Distributed SQL Database](https://www.yugabyte.com/blog/natural-versus-surrogate-primary-keys-in-a-distributed-sql-database/)
- [Primary Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-PRIMARY-KEYS)
