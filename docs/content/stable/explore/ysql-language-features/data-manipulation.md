---
title: Data manipulation
linkTitle: Data manipulation
description: Data manipulation in YSQL
menu:
  stable:
    identifier: explore-ysql-language-features-data-manipulation
    parent: explore-ysql-language-features
    weight: 200
type: docs
---

This section describes how to manipulate data in YugabyteDB using the YSQL `INSERT`, `UPDATE`, and `DELETE` statements.

{{% explore-setup-single %}}

## Insert rows

Initially, database tables are not populated with data. Using YSQL, you can add one or more rows containing complete or partial data by inserting one row at a time.

For example, you work with a database that includes the following table:

```sql
CREATE TABLE employees (
    employee_no integer,
    name text,
    department text
);
```

Assuming you know the order of columns in the table, you can insert a row by executing the following command:

```sql
INSERT INTO employees VALUES (1, 'John Smith', 'Marketing');
```

If you do not know the order of columns, you have an option of listing them in the `INSERT` statement when adding a new row, as follows:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES (1, 'John Smith', 'Marketing');
```

You can view your changes by executing the following command:

```sql
SELECT * FROM employees;
```

You can always view the table schema by executing the following meta-command:

```sql
yugabyte=# \d employees
```

### Default values

In some cases you might not know values for all the columns when you insert a row. You have the option of not specifying these values at all, in which case the columns are automatically filled with default values when the `INSERT`  statement is executed, as demonstrated in the following example:

```sql
INSERT INTO employees (employee_no, name) VALUES (1, 'John Smith');
```

Another option is to explicitly specify the missing values as `DEFAULT` in the `INSERT` statement, as shown in the following example:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES (1, 'John Smith', DEFAULT);
```

### Multiple rows

You can use YSQL to insert multiple rows by executing a single `INSERT`  statement, as shown in the following example:

```sql
INSERT INTO employees
VALUES
(1, 'John Smith', 'Marketing'),
(2, 'Bette Davis', 'Sales'),
(3, 'Lucille Ball', 'Operations');
```

### Upsert

Upsert is a merge during a row insert: when you insert a new table row, YSQL checks if this row already exists, and if so, updates the row; otherwise, a new row is inserted.

The following example creates a table and populates it with data:

```sql
CREATE TABLE employees (
    employee_no integer PRIMARY KEY,
    name text UNIQUE,
    department text NOT NULL
);
```

```sql
INSERT INTO employees
VALUES
(1, 'John Smith', 'Marketing'),
(2, 'Bette Davis', 'Sales'),
(3, 'Lucille Ball', 'Operations');
```

If the department for the employee John Smith changed from Marketing to Sales, the `employees` table could have been modified using the `UPDATE` statement. YSQL provides the `INSERT ON CONFLICT` statement that you can use to perform upserts: if John Smith was assigned to work in both departments, you can use `UPDATE` as the action of the `INSERT` statement, as shown in the following example:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES (1, 'John Smith', 'Sales')
ON CONFLICT (name)
DO
UPDATE SET department = EXCLUDED.department || ';' || employees.department;
```

The following is the output produced by the preceding example:

```output
 employee_no | name          | department
-------------+---------------+-----------------
 1           | John Smith    | Sales;Marketing
 2           | Bette Davis   | Sales
 3           | Lucille Ball  | Operations
```

There are cases when no action is required ( `DO NOTHING` ) if a specific record already exists in the table. For example, executing the following does not change the department for Bette Davis:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES (2, 'Bette Davis', 'Operations')
ON CONFLICT
DO NOTHING;
```

## Load data from a file

The `COPY FROM` statement allows you to populate a table by loading data from a file whose columns are separated by a delimiter character. If the table already has data, the `COPY FROM` statement appends the new data to the existing data by creating new rows. Table columns that are not specified in the `COPY FROM` column list are populated with their default values.

The `filename` parameter of the `COPY` statement enables reading directly from a file.

The following example demonstrates how to use the `COPY FROM` statement:

```sql
COPY employees FROM '/home/mydir/employees.txt.sql' DELIMITER ',' CSV HEADER;
```

## Export data to a file

The `COPY TO` statement allows you to export data from a table to a file. By specifying a column list, you can instruct `COPY TO` to only export data from certain columns.

The `filename` parameter of the `COPY` statement enables copying to a file directly.

The following example demonstrates how to use the `COPY FROM` statement:

```sql
COPY employees TO '/home/mydir/employees.txt.sql' DELIMITER ',';
```

## Back up a database

You can back up a single instance of a YugabyteDB database into a plain-text SQL file by using the  `ysql_dump` is a utility, as follows:

```sh
ysql_dump mydb > mydb.sql
```

To back up global objects that are common to all databases in a cluster, such as roles, you need to use  `ysql_dumpall` .

`ysql_dump` makes backups regardless of whether or not the database is being used.

To reconstruct the database from a plain-text SQL file to the state the database was in at the time of saving, import this file using the `\i` meta-command, as follows:

```sql
yugabyte=# \i mydb
```

## Define NOT NULL constraint

YSQL lets you define various constraints on columns. One of these constraints is [NOT NULL](../../indexes-constraints/other-constraints/#not-null-constraint). You can use it to specify that a column value cannot be null. Typically, you apply this constraint when creating a table, as the following example demonstrates:

```sql
CREATE TABLE employees (
    employee_no integer NOT NULL,
    name text NOT NULL,
    department text
);
```

You may also add the `NOT NULL` constraint to one or more columns of an existing table by executing the `ALTER TABLE` statement, as follows:

```sql
ALTER TABLE employees
ALTER COLUMN department SET NOT NULL;
```

## Defer constraint check

YSQL allows you to set constraints using the `SET CONSTRAINTS` statement and defer foreign key constraints check until the transaction commit time by declaring the constraint `DEFERRED`, as follows:

```sql
BEGIN;
SET CONSTRAINTS name DEFERRED;
...
COMMIT;
```

Note that the `NOT NULL` constraint can't be used with the `SET CONSTRAINTS` statement.

When creating a foreign key constraint that might need to be deferred (for example, if a transaction could have inconsistent data for a while, such as initially mismatched foreign keys), you have an option to define this transaction as `DEFERRABLE` and `INITIALLY DEFERRED`, as follows:

```sql
CREATE TABLE employees (
    employee_no integer,
    name text UNIQUE
    DEFERRABLE INITIALLY DEFERRED
);
```

## Configure automatic timestamps

You can use automatic timestamps to keep track of when data in a table was added or updated.

The date of the data creation is typically added via a  `created_at` column with a default value of `NOW()`, as shown in the following example:

```sql
CREATE TABLE employees (
    employee_no integer NOT NULL,
    name text,
    department text,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
```

To track updates, you need to use triggers that let you define functions executed when an update is performed. The following example shows how to create a function in PL/pgSQL that returns an object called `NEW` containing data being modified:

```sql
CREATE OR REPLACE FUNCTION trigger_timestamp()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;
```

The following examples create a table and connect it with a trigger that executes the `trigger_timestamp` function every time a row is updated in the table:

```sql
CREATE TABLE employees (
    employee_no integer NOT NULL,
    name text,
    department text,
    updated_at TIMESTAMP NOT NULL DEFAULT NOW()
);
```

```sql
CREATE TRIGGER set_timestamp
BEFORE UPDATE ON employees
FOR EACH ROW
EXECUTE PROCEDURE trigger_timestamp();
```

## Obtain modified data

The `RETURNING` clause allows you to obtain data in real time from the rows that you modified using the `INSERT`, `UPDATE`, and `DELETE` statements.

The `RETURNING` clause can contain either column names of its parent statement's target table or value expressions using these columns. To select all columns of the target table, in order, use `RETURNING *`.

When you use the `RETURNING` clause in the  `INSERT` statement, you are obtaining data of the row as it was inserted. This is helpful when dealing with computed default values or with `INSERT ... SELECT` .

When using the `RETURNING` clause in the  `UPDATE` statement, the data you are obtaining from `RETURNING` represents the new content of the modified row, as the following example demonstrates:

```sql
UPDATE employees SET employee_no = employee_no + 1
  WHERE employee_no = 1
  RETURNING name, employee_no AS new_employee_no;
```

In cases of using the `RETURNING` clause in the  `DELETE` statement, you are obtaining the content of the deleted row, as shown the following example:

```sql
DELETE FROM employees WHERE department = 'Sales' RETURNING *;
```

## Auto-Increment column values

Using a special kind of database object called a sequence, you can generate unique identifiers by auto-incrementing the numeric identifier of each preceding row. In most cases, you would use sequences to auto-generate primary keys.

```sql
CREATE TABLE employees2 (employee_no serial, name text, department text);
```

Typically, you add sequences using the `serial` pseudotype that creates a new sequence object and sets the default value for the column to the next value produced by the sequence.

When a sequence generates values, it adds a `NOT NULL` constraint to the column.

The sequence is automatically removed if the  `serial` column is removed.

You can create both a new table and a new sequence generator at the same time, as follows:

```sql
CREATE TABLE employees (
    employee_no serial,
    name text,
    department text
);
```

You may also choose to assign auto-incremented sequence values to new rows created via the `INSERT`  statement. To instruct `INSERT` to take the default value for a column, you can omit this column from the `INSERT` column list, as shown in the following example:

```sql
INSERT INTO employees (name, department) VALUES ('John Smith', 'Sales');
```

Alternatively, you can provide the `DEFAULT` keyword as the column's value, as shown in the following example:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES (DEFAULT, 'John Smith', 'Sales');
```

When you create your sequence via  `serial` , the sequence has all its parameters set to default values. For example, the sequence would not be optimized for access to its information because it does not have a cache (the default value of the a `SEQUENCE` 's  `CACHE` parameter is 1;  `CACHE`  defines how many sequence numbers should be pre-allocated and stored in memory). To be able to configure a sequence at the time of its creation, you need to construct it explicitly and then reference it when you create your table, as shown in the following examples:

```sql
CREATE SEQUENCE sec_employees_employee_no
START 1
CACHE 1000;
```

```sql
CREATE TABLE employees (
    employee_no integer DEFAULT nextval('sec_employees_employee_no') NOT NULL,
    name text,
    department text
);
```

The new sequence value is generated by the `nextval()` function.

## Update rows

YSQL allows you to update a single row in table, all rows, or a set of rows. You can update each column separately.

If you know (1) the name of the table and column that require updating, (2) the rows that need to be modified, and (3) the new value for the column, you can use the `UPDATE`  statement in conjunction with the `SET`  clause to modify data, as shown in the following example:

```sql
UPDATE employees SET department = 'Sales';
```

Because YSQL does not provide a unique identifiers for rows, you might not be able to pinpoint the row directly. To work around this limitation, you can specify one or more conditions a row needs to meet to be updated.

The following example attempts to find an employee whose employee number is 3 and change this number to 7:

```sql
UPDATE employees SET employee_no = 7 WHERE employee_no = 3;
```

If there is no employee number 3 in the table, nothing is updated. If the `WHERE` clause is not included, all  rows in the table are updated; if the `WHERE` clause is included, then only the rows that match the `WHERE` condition are modified.

The new column value does not have to be a constant, as it can be any scalar expression. The following example changes employee numbers of all employees by increasing these numbers by 1:

```sql
UPDATE employees SET employee_no = employee_no + 1;
```

You can use the `UPDATE` statement to modify values of more than one column. You do this by listing more than one assignment in the `SET` clause, as shown in the following example:

```sql
UPDATE employees SET employee_no = 2, name = 'Lee Warren' WHERE employee_no = 5;
```

## Delete rows

Using YSQL, you can remove rows from a table by executing the `DELETE` statement. As with updating rows, you delete specific rows based on one or more conditions that you define in the statement. If you do not provide conditions, you remove all rows.

The following example deletes all rows that have the Sales department:

```sql
DELETE FROM employees WHERE department = 'Sales';
```

You can remove all rows from the table as follows:

```sql
DELETE FROM employees;
```
