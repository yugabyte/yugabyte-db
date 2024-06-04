---
title: Foreign keys in YugabyteDB YSQL
headerTitle: Foreign keys
linkTitle: Foreign keys
description: Defining Foreign key constraint in YSQL
headContent: Explore foreign keys in YugabyteDB using YSQL
menu:
  v2.18:
    identifier: foreign-key-ysql
    parent: explore-indexes-constraints
    weight: 210
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../foreign-key-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

A foreign key represents one or more columns in a table referencing the following:

- A primary key in another table.
- A [unique index](../indexes-1#using-a-unique-index) or columns restricted with a [unique constraint](../other-constraints/#unique-constraint) in another table.

Tables can have multiple foreign keys.

You use a foreign key constraint to maintain the referential integrity of data between two tables: values in columns in one table equal the values in columns in another table.

## Syntax

Define the foreign key constraint using the following syntax:

```sql
[CONSTRAINT fk_name]
  FOREIGN KEY(fk_columns)
    REFERENCES parent_table(parent_key_columns)
    [ON DELETE delete_action]
    [ON UPDATE update_action]
```

Defining the `CONSTRAINT` clause and naming the foreign key is optional. If you omit it, an auto-generated name is provided by YSQL. The `REFERENCES` clause specifies the parent table and its columns referenced by the *fk_columns*. Defining actions is also optional; if defined, they determine the behaviors when the primary key in the parent table is deleted or updated. YSQL allows you to perform the following actions:

- `SET NULL` - when the referenced rows in the parent table are deleted or updated, foreign key columns in the referencing rows of the child table are automatically set to `NULL`.
- `SET DEFAULT` - when the referenced rows of the parent table are deleted or updated, the default value is set to the foreign key column of the referencing rows in the child table.
- `RESTRICT` - when the referenced rows in the parent table are deleted or updated, deletion of a referenced row is prevented.
- `CASCADE` - when the referenced rows in the parent table are deleted or updated, the referencing rows in the child table are deleted or updated.
- `NO ACTION` (default) - when the referenced rows in the parent table are deleted or updated, no action is taken.

## Examples

{{% explore-setup-single %}}

The following example creates two tables:

```sql
CREATE TABLE employees(
  employee_no integer GENERATED ALWAYS AS IDENTITY,
  name text NOT NULL,
  department text,
  PRIMARY KEY(employee_no)
);

CREATE TABLE contacts(
  contact_id integer GENERATED ALWAYS AS IDENTITY,
  employee_no integer,
  contact_name text NOT NULL,
  phone integer,
  email text,
  PRIMARY KEY(contact_id),
  CONSTRAINT fk_employee
    FOREIGN KEY(employee_no)
      REFERENCES employees(employee_no)
);
```

The parent table is `employees` and the child table is `contacts`. Each employee has any number of contacts, and each contact belongs to no more than one employee. The `employee_no` column in the `contacts` table is the foreign key column that references the primary key column with the same name in the `employees` table. The `fk_employee` foreign key constraint in the `contacts` table defines the `employee_no` as the foreign key. By default, `NO ACTION` is applied because `fk_employee` is not associated with any action.

The following example shows how to create the same `contacts` table with a `CASCADE` action `ON DELETE`:

```sql
CREATE TABLE contacts(
  contact_id integer GENERATED ALWAYS AS IDENTITY,
  employee_no integer,
  contact_name text NOT NULL,
  phone integer,
  email text,
  PRIMARY KEY(contact_id),
  CONSTRAINT fk_employee
    FOREIGN KEY(employee_no)
      REFERENCES employees(employee_no)
      ON DELETE CASCADE
);
```

## Use ALTER TABLE to add or drop a Foreign Key Constraint

You can add a foreign key constraint to an existing table by using the `ALTER TABLE` statement, using the following syntax:

```sql
ALTER TABLE child_table
  ADD CONSTRAINT constraint_name
    FOREIGN KEY (fk_columns)
      REFERENCES parent_table (parent_key_columns);
```

Before altering a table with a foreign key constraint, you need to remove the existing foreign key constraint, as per the following syntax:

```sql
ALTER TABLE child_table
  DROP CONSTRAINT constraint_fkey;
```

The next step is to add a new foreign key constraint, possibly including an action, as demonstrated by the following syntax:

```sql
ALTER TABLE child_table
  ADD CONSTRAINT constraint_fk
    FOREIGN KEY (fk_columns)
      REFERENCES parent_table(parent_key_columns)
      [ON DELETE action];
```

## Learn more

- [Table with Foreign Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-foreign-key-constraint)
- [Foreign Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-FK)
- [Relational Data Modeling with Foreign Keys in a Distributed SQL Database](https://www.yugabyte.com/blog/relational-data-modeling-with-foreign-keys-in-a-distributed-sql-database/)
- [Other Constraints](../other-constraints/)
