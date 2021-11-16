---
title: Constraints
linkTitle: Constraints
description: Defining constraints
headcontent: Constraints
image: /images/section_icons/secure/create-roles.png
menu:
  v2.6:
    identifier: constraints
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

YSQL allows you to define primary and foreign keys, as well as check values based on various criteria.

## Primary Key

A primary key is a means to identify a specific row in a table via one or more columns. To define a primary key, you create a constraint that is, functionally, a [unique index](../indexes-1/#using-a-unique-index) applied to the table columns. 

Most commonly, the primary key is added to the table when the table is created, as demonstrated by the following syntax:

```sql
CREATE TABLE (
  column1 data_type PRIMARY KEY,
  column2 data_type,
  column3 data_type,
  …
);
```

Suppose you work with a database that requires the following table to be created:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text
);
```

The primary key of the `employees` table is `employee_no`, which uniquely identifies an employee.

The following syntax the primary key definition for more than one column:

```sql
CREATE TABLE (
  column1 data_type,
  column2 data_type,
  column3 data_type,
  …
  PRIMARY KEY (column1, column2)
);
```

The following example creates the `employees` table in which the primary key is a combination of `employee_no` and `name`:

```sql
CREATE TABLE employees (
  employee_no integer,
  name text,
  department text,
  PRIMARY KEY (employee_no, name)
);
```

YSQL assigns a default name in the format `tablename_pkey` to the primary key constraint. In the preceding example, it is `employees_pkey`. If you need a different name, you can specify it using the `CONSTRAINT` clause, as per the following syntax:

```sql
CONSTRAINT constraint_name PRIMARY KEY(column1, column2, ...);
```

In some cases you might decide to define a primary key for an existing table. To do this, you use the `ALTER TABLE` statement, as per the following syntax:

```sql
ALTER TABLE table_name ADD PRIMARY KEY (column1, column2);
```

The `ALTER TABLE` statement also allows you to add an auto-incremented primary key to an existing table by using the [SERIAL type](../../ysql-language-features/data-types/#serial-pseudotype), as per the following syntax:

```sql
ALTER TABLE table_name ADD COLUMN ID SERIAL PRIMARY KEY;
```

You can remove a primary key constraint by using the `ALTER TABLE` statement, as demonstrated by the following syntax:

```sql
ALTER TABLE table_name DROP CONSTRAINT primary_key_constraint;
```

The following example shows how to remove the primary key constraint from the `employees` table:

```sql
ALTER TABLE employees DROP CONSTRAINT employees_pkey;
```

For more information and examples, refer to the following: 

- [Primary Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key)
- [Table with Primary Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-primary-key)
- [Primary Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-PRIMARY-KEYS)

## Foreign Key

A foreign key represents one or more columns in a table referencing the following: 

- A primary key in another table.
- A [unique index](../indexes-1#using-a-unique-index) or columns restricted with a [unique constraint](#unique-constraint) in another table.

Tables can have multiple foreign keys.

You use a foreign key constraint to maintain the referential integrity of data between two tables: values in columns in one table equal the values in columns in another table.

You define the foreign key constraint using the following syntax:

```sql
[CONSTRAINT fk_name] 
  FOREIGN KEY(fk_columns) 
    REFERENCES parent_table(parent_key_columns)
    [ON DELETE delete_action]
    [ON UPDATE update_action]
```

Defining the `CONSTRAINT` clause and naming the foreign key is optional. If you omit it, an auto-generated name is provided by YSQL. The `REFERENCES` clause specifies the parent table and its columns referenced by the *fk_columns*. Defining actions is also optional; if defined, they determine the behaviors when the primary key in the parent table is deleted or updated. YSQL allows you to perform the following actions: 

- `SET NULL` - when the referenced rows in the parent table are deleted or updated, foreign key columns in the referencing rows of the child table are automatically set to `NULL` .
- `SET DEFAULT` - when the referenced rows of the parent table are deleted or updated, the default value is set to the foreign key column of the referencing rows in the child table. 
- `RESTRICT` - when the referenced rows in the parent table are deleted or updated, deletion of a referenced row is prevented.
- `CASCADE` - when the referenced rows in the parent table are deleted or updated, the referencing rows in the child table are deleted or updated.
- `NO ACTION` (default) - when the referenced rows in the parent table are deleted or updated, no action is taken.

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

In the preceding example, the parent table is `employees` and the child table is `contacts`. Each employee has any number of contacts, and each contact belongs to no more than one employee. The `employee_no` column in the `contacts` table is the foreign key column that references the primary key column with the same name in the `employees` table. The `fk_customer` foreign key constraint in the `contacts` table defines the `employee_no` as the foreign key. Since `fk_customer` is not associated with any action, `NO ACTION` is applied by default.

The following example shows how to create the same `contacts` table with a `CASCADE` action `ON DELETE`:

```sql
CREATE TABLE contacts(
  ...
  REFERENCES employees_1(employee_no)
  ON DELETE CASCADE
);
```

YSQL enables you to ads a foreign key constraint to an existing table by using the `ALTER TABLE` statement, as demonstrated by the following syntax:

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

For more information and examples, see the following:

- [Foreign Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#foreign-key)
- [Table with Foreign Key](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-foreign-key-constraint)
- [Foreign Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-FK)

## CHECK Constraint

The YSQL `CHECK` constraints allow you to constrain values in columns based on a boolean expression. The values are evaluated with regards to meeting a specific requirement before these values are inserted or updated; if they fail the check, YSQL rejects the changes and displays a constraint violation error.

In most cases, you add the `CHECK` constraint when you create a table, as demonstrated by the following example:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  birth DATE CHECK (birth > '1940-01-01'),
  salary numeric CHECK (salary > 10)
);
```

The following example attempts to insert a row that violates the `CHECK` constraint into the `employees` table:

```sql
INSERT INTO employees (employee_no, name, department, birth, salary) 
  VALUES (2001, 'Hugh Grant', 'Sales', '1963-05-05', 0);
```

The following output shows that the execution of the `INSERT` statement failed because of the `CHECK` constraint on the `salary` column which only accepts values greater than 10:

```output
ERROR: new row for relation "employees" violates check constraint "employees_salary_check"
DETAIL: Failing row contains (2001, Hugh Grant, Sales, 1963-05-05, 0).
```

The preceding output shows the name of the `CHECK` constraint as `employees_salary_check` which was assigned by default based on the table_column_check pattern. If you need a specific name for the `CHECK` constraint, you can set it, as per the following example:

```sql
(
  ...
  salary numeric CONSTRAINT fair_salary CHECK (salary > 10)
  ...
);
```

YSQL also allows you to add `CHECK` constraints to existing tables by using the `ALTER TABLE` statement. The following example shows how to add a length check for the employee name in the `employees` table:

```sql
ALTER TABLE employees
  ADD CONSTRAINT name_check CHECK (char_length(name) <= 3);
```

For additional examples, see [Table with CHECK constraint](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-check-constraint).

## UNIQUE Constraint

The `UNIQUE` constraint allows you to ensure that values stored in columns are unique across rows in a table. During inserting new rows or updating existing ones, the `UNIQUE` constraint checks if the value is already in the table, in which case the change is rejected and an error is displayed.

When you add a `UNIQUE` constraint to one or more columns, YSQL automatically creates a [unique index](../indexes-1#using-a-unique-index) on these columns.

The following example creates a table with a `UNIQUE` constraint for the `phone` column:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer UNIQUE
);
```

The following example creates the same constraint for the same column of the same table, only as a table constraint:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer, 
  UNIQUE(phone)
);
```

The following example creates a `UNIQUE` constraint on a group of columns in a new table:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer,
  email text
  UNIQUE(phone, email)
);
```

For additional examples, see [Table with UNIQUE constraint](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-unique-constraint).

## NOT NULL Constraint

YSQL provides a `NOT NULL` constraint as a means to control whether or not a column can accept `NULL` values. If a column has a `NOT NULL` constraint set, any attempt to insert a `NULL` value or update it with a `NULL` value results in an error.

For additional information and examples, see the following:

- [Defining NOT NULL Constraint](../../ysql-language-features/data-manipulation/#defining-not-null-constraint)
- [Not-Null Constraints in PostgreSQL documentation](https://www.postgresql.org/docs/11/ddl-constraints.html#id-1.5.4.5.6)
