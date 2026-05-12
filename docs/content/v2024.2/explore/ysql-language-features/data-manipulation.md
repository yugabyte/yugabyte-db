---
title: Writing data
linkTitle: Write data
headerTitle: INSERT, UPDATE, and DELETE
description: Data manipulation - Insert/Modify/Delete in YSQL
headcontent: Understand how to insert new data and modify or delete existing data
menu:
  v2024.2:
    identifier: explore-ysql-language-features-data-manipulation
    parent: explore-ysql-language-features
    weight: 400
type: docs
---

INSERT, UPDATE, and DELETE (often abbreviated as IUD) are fundamental SQL operations used to modify data in a database. This page provides an overview of how to use these commands effectively to manipulate data in a database. You'll learn how to insert new rows into tables, update specific records based on conditions, and delete rows that are no longer needed. Understanding these operations is crucial for maintaining accurate and up-to-date information in your database.

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

For illustration, consider the following employee schema:

```sql
CREATE TABLE employees (
    id INTEGER PRIMARY KEY,
    name TEXT,
    department TEXT DEFAULT 'Engineering'
);
```

## Simple insert

The INSERT statement in SQL is used to add new rows of data into a table. It specifies the table to insert data into, followed by the columns and the corresponding values. A simple INSERT statement is the most basic way to populate a table with new information. To add a row on to the employees table, run the following:

```sql
INSERT INTO employees (id, name, department) VALUES (1, 'Johnny Depp', 'Marketing');
```

If you are certain about the order of the columns in the table, you can skip mentioning the column names and just specify the value:

```sql
INSERT INTO employees VALUES (1, 'Johnny Depp', 'Marketing');
```

## Multiple rows

You can insert multiple rows in a single INSERT statement by specifying multiple values, as shown in the following example:

```sql
INSERT INTO employees VALUES
    (1, 'Johnny Depp', 'Marketing'),
    (2, 'Brad Pitt', 'Sales'),
    (3, 'Angelina Jolie', 'Operations');
```

## Default values

Default values allow you to define a value that will automatically be inserted into a column if no explicit value is provided during an INSERT operation. This is useful for ensuring that columns always have meaningful data, even when some values are not specified by the user.

When creating the `employees` table, we set a default value for the column `department` using the DEFAULT clause (`department TEXT DEFAULT 'Engineering'`). This lets you insert a row without explicitly specifying a department for a row:

```sql
INSERT INTO employees (id, name) VALUES (4, 'Bruce Lee');
```

Another option is to explicitly specify the missing values as DEFAULT in the INSERT statement when column names are specified explicitly as shown in the following example:

```sql
INSERT INTO employees (id, name, department)
    VALUES (5, 'Al Pacino', DEFAULT);
```

## Current timestamp

You can use current timestamps to keep track of when data in a table was added or updated. This can be accomplished column by setting a default value of `NOW()`, as follows:

```sql
CREATE TABLE employees3 (
    id integer NOT NULL,
    name text,
    department text,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
```

This ensures that the latest timestamp is automatically added to the row when it is first inserted.

{{<tip>}}
To update the timestamp on row modification, you would use [Triggers](../advanced-features/triggers/#create-triggers).
{{</tip>}}

## Auto-Increment

Using [Sequences](/stable/develop/data-modeling/primary-keys-ysql/#sequence), you can generate unique identifiers by auto-incrementing the numeric identifier of each preceding row. In most cases, you would use sequences to auto-generate primary keys.

Although you can assign a default value to a column via a sequence, typically you add sequences using the [serial](/stable/develop/data-modeling/primary-keys-ysql/#serial) pseudotype that creates a new sequence object and sets the default value for the column to the next value produced by the sequence. For example:

```sql
CREATE TABLE employees2 (
    id serial,
    name text,
    department text
);
```

This allows you to omit the serial column during INSERT as in the following example:

```sql
INSERT INTO employees2 (name, department) VALUES ('Johnny Depp', 'Sales');
```

Alternatively, you can provide the DEFAULT keyword as the column's value, as shown in the following example:

```sql
INSERT INTO employees2 (id, name, department) VALUES (DEFAULT, 'Johnny Depp', 'Sales');
```

## Update rows

The UPDATE statement is used to modify existing data in a table. With this command, you can change the values of one or more columns for rows that meet specified conditions, precisely controlling which records need to be altered. Typically UPDATE is used in conjunction with the SET statement.

To update the name of a specific employee, you can specify a WHERE clause, like so:

```sql
UPDATE employees SET name = 'Dwayne Jhonson' WHERE id = 2;
```

If you decide to rename your `Marketing` department to `Brand Management`, you can update all the `Marketing` data as follows:

```sql
UPDATE employees SET department = 'Brand Management' WHERE department = 'Marketing';
```

To update the department of all employees to `Sales`, you can run the following:

```sql
UPDATE employees SET department = 'Sales';
```

You can also perform mathematical operations on columns where permitted. For example, you can change the IDs for all employees by incrementing by 1:

```sql
UPDATE employees SET id = id + 1;
```

## Upsert

An UPSERT is a combination of UPDATE and INSERT, allowing you to either insert a new row into a table or update an existing row if it already exists. This operation is particularly useful for avoiding duplication while ensuring data is either added or updated as needed. This functionality is provided by the INSERT ... ON CONFLICT DO UPDATE clause.

For example, if you try to insert the record `(1, 'Johnny Depp', 'Sales')`, you get an error:

```sql{.nocopy}
ERROR:  23505: duplicate key value violates unique constraint "employees_pkey"
```

This is because the record `(1, 'Johnny Depp', 'Marketing')` already exists. Instead of having an error thrown, you can ask the server to ignore the insert using DO NOTHING:

```sql
INSERT INTO employees (id, name, department)
    VALUES (1, 'Johnny Depp', 'Sales')
    ON CONFLICT (id)
    DO NOTHING;
```

This would return `INSERT 0 0` to signify no rows were inserted. Alternatively, you can choose to update any other column other than the conflicted column (say, department) like so:

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

The DELETE statement in SQL is used to remove one or more rows from a table based on a specified condition. This operation is crucial for maintaining and managing the integrity of data in a database.

{{<tip>}}
To remove entire tables or databases you use the DROP statement. The DELETE statement allows for more granular control by targeting specific records.
{{</tip>}}

For example, to remove a specific a employee, you can run:

```sql
DELETE FROM employees WHERE id = 1;
```

To remove all employees in the `Sales` department, you can run:

```sql
DELETE FROM employees WHERE department = 'Sales';
```

## Retrieve affected rows

To return specific column values directly after data modification via INSERT, UPDATE, or DELETE operations, without the need for a separate SELECT query, you can use the RETURNING statement.

For example, say you want to update the ID of an employee, and in the same statement you want to get back the name and the new ID. Do the following:

```sql
UPDATE employees SET id = id + 10
  WHERE id = 1
  RETURNING name, id;
```

In cases of row deletion, you can retrieve the contents of the deleted row, as follows:

```sql
DELETE FROM employees WHERE department = 'Sales' RETURNING *;
```

## Constraints

Constraints enforce rules on the data to ensure accuracy and consistency, such as preventing duplicate values or maintaining relationships between tables. Constraints ensure data validity and integrity, preventing invalid data from being inserted or updated.

### CHECK Constraint

The CHECK constraint in SQL is used to enforce specific rules on the values that can be entered into a column. It ensures that all data in a table meets predefined conditions, improving data integrity by preventing invalid entries. For example, in the employees table you add a validation rule to check for sanity of the department name like so:

```sql
DROP TABLE IF EXISTS employees4;
CREATE TABLE employees4 (
  id integer PRIMARY KEY,
  name text,
  department text CHECK (char_length(department) >= 3)
);
```

{{<note>}}
You can add an explicit name for the constraint. For example:

`department text CHECK valid_dept (char_length(department) >= 3)`
{{</note>}}

Now when you insert an invalid department name into the table, like this:

```sql
INSERT INTO employees4 VALUES(2, 'John', 'X');
```

An error is thrown:

```sql{.nocopy}
ERROR:  23514: new row for relation "employees4" violates check constraint "employees4_department_check"
```

You can add a check constraint after the table is created using ALTER TABLE.

```sql
ALTER TABLE employees4
  ADD CONSTRAINT id_check CHECK (id > 0);
```

### UNIQUE constraint

The UNIQUE constraint ensures that all values in a specified column (or a group of columns) are distinct across the table, preventing duplicate entries. For example, you can enforce uniqueness of phone number in your employee table as follows:

```sql
CREATE TABLE employees5 (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer UNIQUE
);
```

When a record with a phone number that already exists is inserted into the table, an error is thrown:

```sql{.nocopy}
ERROR:  23505: duplicate key value violates unique constraint "employees5_phone_key"
```

### NOT NULL constraint

The NOT NULL constraint ensures that a column cannot store NULL values, meaning that every row in the table must have a value for this column. For example, you can add a constraint to ensure that the employee name is always valid as follows:

```sql
CREATE TABLE employees6 (
    employee_no integer NOT NULL,
    name text NOT NULL,
    department text
);
```

When a NULL value for name is inserted, you get an error:

```sql{.nocopy}
ERROR:  23502: null value in column "name" violates not-null constraint
```

### Foreign key constraint

Foreign Key constraints are used to enforce referential integrity between two tables in a relational database. They create a link between data in two tables, ensuring that relationships between tables remain consistent. Foreign keys help prevent orphaned records by ensuring that references to related data remain valid. For example, consider the scenario where you have a `departments` table that has `dept_id` and other info, and an employee table that links the employee using `dept_id`.

```sql
CREATE TABLE departments (
    dept_id INT PRIMARY KEY,
    dept_name VARCHAR(100)
);

CREATE TABLE employees (
    emp_id INT PRIMARY KEY,
    emp_name VARCHAR(100),
    dept_id INT,
        CONSTRAINT fk_department FOREIGN KEY (dept_id)
            REFERENCES departments(dept_id)
);
```

As you have a foreign key constraint set up between these tables, you won't be able to insert a row into the `employees` tables with an invalid `dept_id`. It will throw a `violates foreign key constraint` error:

```sql
INSERT INTO employees VALUES(1, 'Brian', 2000);
```

```sql{.nocopy}
ERROR:  23503: insert or update on table "employees" violates foreign key constraint "fk_department"
```

### Deferring constraints

By default, constraints are checked immediately after each statement (INITIALLY IMMEDIATE), but with INITIALLY DEFERRED, the check is postponed until the transaction is committed. This is particularly useful when performing complex operations that might temporarily violate constraints but are resolved by the time the transaction is complete, particularly in the context of foreign key constraints.

Using DEFERRABLE INITIALLY DEFERRED allows you to perform complex changes in a transaction without constraint violations, making sure that integrity checks are only performed when the transaction is about to complete. Without this, the following transaction will error out in Step 1.

```sql
BEGIN;
    -- Step 1: Insert a new the employee's info
    -- NOTE: dept `2000` does not exist as of now
    INSERT INTO employees VALUES(1, 'Brian', 2000);

    -- Step 2: Insert the new department
    INSERT INTO department VALUES(2000, 'Area-51');

-- Commit the transaction, which checks the constraints at this point
COMMIT;
```

But when you add the DEFERRABLE INITIALLY DEFERRED clause, the preceding transaction succeeds.

```sql
    dept_id INT,
        CONSTRAINT fk_department FOREIGN KEY (dept_id)
            REFERENCES departments(dept_id) DEFERRABLE INITIALLY DEFERRED
```
