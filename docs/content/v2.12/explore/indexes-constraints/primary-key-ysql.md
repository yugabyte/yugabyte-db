---
title: Primary key
linkTitle: Primary key
description: Defining Primary key constraint in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.12:
    identifier: primary-key-ysql
    parent: explore-indexes-constraints
    weight: 200
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../primary-key-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../primary-key-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The Primary Key constraint is a means to identify a specific row in a table uniquely via one or more columns. To define a primary key, you create a constraint that is, functionally, a [unique index](../indexes-1/#using-a-unique-index) applied to the table columns.

## Syntax and examples

- To run the examples below, follow these steps to create a local [cluster](/preview/quick-start/) or in [Yugabyte Cloud](/preview/yugabyte-cloud/cloud-connect/).

- Use the [YSQL shell](/preview/admin/ysqlsh/) for local clusters, or [Connect using Cloud shell](/preview/yugabyte-cloud/cloud-connect/connect-cloud-shell/) for Yugabyte Cloud, and create the yb_demo [database](/preview/yugabyte-cloud/cloud-quickstart/qs-data/#create-a-database).

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

The following example creates the `employee` table with `employee_no` as the primary key, which uniquely identifies an employee.

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

- Use the `ALTER TABLE` statement to create a primary key on an existing table with following syntax:

```sql
ALTER TABLE table_name ADD PRIMARY KEY (column1, column2);
```

The following example creates the `employee` table first and then alters it to add a primary key on the `employee_no` column:

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

- [Primary Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key)
- [Table with Primary Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-primary-key)
- [Natural versus Surrogate Primary Keys in a Distributed SQL Database](https://www.yugabyte.com/blog/natural-versus-surrogate-primary-keys-in-a-distributed-sql-database/)
- [Primary Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-PRIMARY-KEYS)
