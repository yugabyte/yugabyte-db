---
title: Writing data
linkTitle: Write data
headcontent: INSERT, UPDATE, and DELETE
description: Data manipulation - Insert/Modify/Delete in YSQL
headcontent: Understand how to insert new data and modify or delete existing data
menu:
  preview:
    identifier: explore-ysql-language-features-data-manipulation
    parent: explore-ysql-language-features
    weight: 500
type: docs
---

Insert, Update, and Delete (often abbreviated as IUD) are fundamental SQL operations used to modify data in a database. This page provides an overview of how to use these commands effectively to manipulate data within a database. You'll learn how to insert new rows into tables, update specific records based on conditions, and delete rows that are no longer needed. Understanding these operations is crucial for maintaining accurate and up-to-date information in your database.

## Setup

The examples will run on any YugabyteDB universe. To create a universe follow the instructions below.

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

For illustration, let us consider the following employee schema,

```sql
CREATE TABLE employees (
    id INTEGER PRIMARY KEY,
    name TEXT,
    department TEXT DEFAULT 'Engineering'
);
```

## Simple insert

The INSERT statement in SQL is used to add new rows of data into a table. It specifies the table to insert data into, followed by the columns and the corresponding values. A simple INSERT statement is the most basic way to populate a table with new information. To add a row on to the employees table, you can run,

```sql
INSERT INTO employees (id, name, department) VALUES (1, 'Johnny Depp', 'Marketing');
```

If you are certain about the order of the columns in the table, you can skip mentioning the column names and just specify the value like:

Assuming you know the order of columns in the table, you can insert a row by executing the following command:

```sql
INSERT INTO employees VALUES (1, 'Johnny Depp', 'Marketing');
```

## Multiple rows

You can insert multiple rows in a single `INSERT` statement by specifying multiple values, as shown in the following example:

```sql
INSERT INTO employees VALUES
    (1, 'Johnny Depp', 'Marketing'),
    (2, 'Brad Pitt', 'Sales'),
    (3, 'Angelina Jolie', 'Operations');
```

## Default values

Default values allow you to define a value that will automatically be inserted into a column if no explicit value is provided during an INSERT operation. This is useful for ensuring that columns always have meaningful data, even when some values are not specified by the user.

```sql
INSERT INTO employees (id, name) VALUES (4, 'Bruce Lee');
```

Another option is to explicitly specify the missing values as `DEFAULT` in the `INSERT` statement, as shown in the following example:

```sql
INSERT INTO employees (id, name, department)
    VALUES (5, 'Al Pacino', DEFAULT);
```

## Current timestamp

You can use current timestamps to keep track of when data in a table was added or updated. This can be accomplished column by setting a default value of `NOW()` , as shown below.

```sql
CREATE TABLE employees3 (
    id integer NOT NULL,
    name text,
    department text,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
```

This will ensure that the latest timestamp is automatically added to the row when it is first inserted.

{{<tip>}}
To update the timestamp on row modification, you would need to use [Triggers](../advanced-features/triggers/#create-triggers)
{{</tip>}}

## Auto-Increment

Using [Sequences](../../../develop/data-modeling/primary-keys-ysql/#sequence) , you can generate unique identifiers by auto-incrementing the numeric identifier of each preceding row. In most cases, you would use sequences to auto-generate primary keys.

Although you can assign a default value to a column [via a sequence](../../../develop/data-modeling/primary-keys-ysql/#sequence) typically, you add sequences using the [serial](../../../develop/data-modeling/primary-keys-ysql/#serial) pseudotype that creates a new sequence object and sets the default value for the column to the next value produced by the sequence like:

```sql
CREATE TABLE employees2 (
    id serial,
    name text,
    department text
);
```

This allows you to omit the serial column during `INSERT` as in the following example:

```sql
INSERT INTO employees2 (name, department) VALUES ('Johnny Depp', 'Sales');
```

Alternatively, you can provide the `DEFAULT` keyword as the column's value, as shown in the following example:

```sql
INSERT INTO employees2 (id, name, department) VALUES (DEFAULT, 'Johnny Depp', 'Sales');
```

## Update rows

The UPDATE statement is used to modify existing data in a table. With this command, you can change the values of one or more columns for rows that meet specified conditions, precisely controlling which records need to be altered. Typically UPDATE is used in conjunction with the SET statement.

To update the name of a specific employee, you can run specify a WHERE clause like

```sql
UPDATE employees SET name = 'Dwayne Jhonson' WHERE id = 2;
```

If you decide to rename your `Marketing` department to `Brand Management`, you can update all the `Marketing` data as:

```sql
UPDATE employees SET department = 'Brand Management' WHERE department = 'Marketing';
```

To update the department of all employees to `Sales`, you can run

```sql
UPDATE employees SET department = 'Sales';
```

You can also perform mathematical operations on columns where permitted. For example, you can change the ids for all employees by incrementing by 1 like:

```sql
UPDATE employees SET id = id + 1;
```

## Upsert

An `UPSERT` is a combination of "UPDATE" and "INSERT," allowing you to either insert a new row into a table or update an existing row if it already exists. This operation is particularly useful for avoiding duplication while ensuring data is either added or updated as needed. This functionality is provided by the `INSERT ... ON CONFLICT DO UPDATE` clause.

For example, if we try to insert record `(1, 'Johnny Depp', 'Sales')` you will get an error like:

```sql{.nocopy}
ERROR:  23505: duplicate key value violates unique constraint "employees_pkey"
```

This is because, the record `(1, 'Johnny Depp', 'Marketing')` already exists. Now instead of an error to be thrown, we can either ask the server to ignore the insert with `DO NOTHING` like:

```sql
INSERT INTO employees (id, name, department)
    VALUES (1, 'Johnny Depp', 'Sales')
    ON CONFLICT (id)
    DO NOTHING;
```

which would return `INSERT 0 0` to signify no rows were inserted, or we can choose to update the any other column other than the conflicted column (say, department) like:

```sql
INSERT INTO employees (id, name, department)
    VALUES (1, 'Johnny Depp', 'Sales')
    ON CONFLICT (id)
    DO UPDATE SET department = EXCLUDED.department || ', ' || employees.department;
```

Now if you select the rows from the table, you will see that the department for `Johnny Depp` has been updated to `Sales, Marketing`.

```caddyfile{.nocopy}
 id |      name      |    department
----+----------------+------------------
  1 | Johnny Depp    | Sales, Marketing
  2 | Brad Pitt      | Sales
  3 | Angelina Jolie | Operations
```

## Delete rows

The DELETE statement in SQL is used to remove one or more rows from a table based on a specified condition. This operation is crucial for maintaining and managing the integrity of data within a database.

{{<tip>}}
To remove entire tables or databases you would need to use the DROP statement. The DELETE statement allows for more granular control by targeting specific records.
{{</tip>}}

For example to remove a specific a employee, you can run:

```sql
DELETE FROM employees WHERE id = 1;
```

And to remove all employees in the `Sales` department, you can run

```sql
DELETE FROM employees WHERE department = 'Sales';
```

## Retrieve affected rows

To return specific column values directly after data modification via INSERT, UPDATE, or DELETE operations, without the need for a separate SELECT query you can use the RETURNING statement.

For example, say you want tp update the id of an employee and in the same statement, you want to get back the name and the new id. For this you can do:

```sql
UPDATE employees SET id = id + 10
  WHERE id = 1
  RETURNING name, id;
```

In cases of row deletion, you can retrieve the contents of the deleted row, as shown the following example:

```sql
DELETE FROM employees WHERE department = 'Sales' RETURNING *;
```

## Constraints

Constraints enforce rules on the data to ensure accuracy and consistency, such as preventing duplicate values or maintaining relationships between tables. Constraints ensure data validity and integrity, preventing invalid data from being inserted or updated.

### CHECK Constraint

The CHECK constraint in SQL is used to enforce specific rules on the values that can be entered into a column. It ensures that all data in a table meets predefined conditions, improving data integrity by preventing invalid entries. For example, in the employees table you add a validation rule to check for sanity of the department name like:

```sql
DROP TABLE IF EXISTS employees4;
CREATE TABLE employees4 (
  id integer PRIMARY KEY,
  name text,
  department text CHECK (char_length(department) >= 3)
);
```

{{<note>}}
You can add a explict name for the constraint like `department text CHECK valid_dept (char_length(department) >= 3)`
{{</note>}}

Now when you insert into the table an invalid department name like:

```sql
INSERT INTO employees4 VALUES(2, 'John', 'X');
```

an error would be thrown like:

```sql{.nocopy}
ERROR:  23514: new row for relation "employees4" violates check constraint "employees4_department_check"
DETAIL:  Failing row contains (2, John, X).
SCHEMA NAME:  public
TABLE NAME:  employees4
CONSTRAINT NAME:  employees4_department_check
```

You can add a check constraint after the table is created with ALTER TABLE.

```sql
ALTER TABLE employees4
  ADD CONSTRAINT id_check CHECK (id > 0);
```

### UNIQUE Constraint

The UNIQUE constraint ensures that all values in a specified column (or a group of columns) are distinct across the table, preventing duplicate entries. For example, you can enforce uniqueness of phone number in your employee table like:

```sql
CREATE TABLE employees5 (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer UNIQUE
);
```

When a record with a phone number already existing in the table is inserted, an error will be thrown as below:

```sql{.nocopy}
ERROR:  23505: duplicate key value violates unique constraint "employees5_phone_key"
```

### NOT NULL Constraint

The NOT NULL constraint ensures that a column cannot store NULL values, meaning that every row in the table must have a value for this column. For example, you can add a constraint to ensure that the employee name is always valid like:

```sql
CREATE TABLE employees6 (
    employee_no integer NOT NULL,
    name text NOT NULL,
    department text
);
```

When a NULL value for name is inserted, you will get an error as:

```sql{.nocopy}
ERROR:  23502: null value in column "name" violates not-null constraint
```

### Deferring constraints