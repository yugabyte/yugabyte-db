---
title: Triggers
linkTitle: Triggers
description: Triggers in YSQL
menu:
  stable:
    identifier: explore-ysql-language-features-triggers
    parent: explore-ysql-language-features
    weight: 310
type: docs
---

This document describes how to use triggers when performing data manipulation and definition.

{{% explore-setup-single %}}

## Overview

In YSQL, a function invoked automatically when an event associated with a table occurs is called a trigger. The event is typically caused by modification of data during `INSERT`, `UPDATE`, and `DELETE`. The even can also be caused by schema changes.

You create a trigger by defining a function and then attaching this trigger function to a table. You can create a trigger at a row level or a statement level, depending on the number of times the trigger is to be invoked and when. For example, when executing an `UPDATE` statement that affects five rows, the row-level trigger is invoked five times, whereas the statement-level trigger is invoked only once. In addition, you can enable the trigger to be invoked before or after an event: when the trigger is invoked before an event, it may skip the operation for the current row or modify the row itself; if the trigger is invoked after the event, the trigger has access to all the changes.

When YugabyteDB is being used by different applications, triggers allow you to keep the cross-functional behavior in the database that is performed automatically every time the table data is modified. In addition, triggers let you maintain data integrity rules that can only be implemented at the database level.

When using triggers, it is important to be aware of their existence and understand their logic. Otherwise, it is difficult to predict the timing and effects of data changes.

## Create triggers

Creating a trigger in YSQL is a two-step process: you start by creating a trigger function via the `CREATE FUNCTION` statement, and then you bind the trigger function to a table using the `CREATE TRIGGER` statement.

The `CREATE FUNCTION` statement has the following syntax:

```sql
CREATE FUNCTION trigger_function()
RETURNS TRIGGER LANGUAGE PLPGSQL
AS $$
BEGIN
   -- ...
END;
$$
```

*trigger_function* obtains information about the environment that is invoking it via a container populated with local variables.

The `CREATE TRIGGER` statement has the following syntax:

```sql
CREATE TRIGGER tr_name
{BEFORE | AFTER} { event }
ON tbl_name [FOR [EACH] { ROW | STATEMENT }]
       EXECUTE PROCEDURE trigger_function
```

The trigger *tr_name* fires before or after *event* which can be set to `INSERT` , `DELETE`, `UPDATE`, or `TRUNCATE`. *tbl_name* represents the table associated with the trigger. If you use the `FOR EACH ROW` clause, the scope of the trigger would be one row. If you use the `FOR EACH STATEMENT` clause, the trigger would be fired for each statement. *trigger_function* represents the procedure to be performed when the trigger is fired.

### Example

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text
);
```

```sql
INSERT INTO employees VALUES
(1221, 'John Smith', 'Marketing'),
(1222, 'Bette Davis', 'Sales'),
(1223, 'Lucille Ball', 'Operations'),
(1224, 'John Zimmerman', 'Sales');
```

If an employee is transferred to a different department, the change is recorded in a table called `employee_dept_changes`, as shown in the following example:

```sql
CREATE TABLE employee_dept_changes (
  employee_no integer NOT NULL,
  name text,
  department text,
  changed_on TIMESTAMP(6) NOT NULL
);
```

To start recording changes, you create a function called `record_dept_changes`, as shown in the following example:

```sql
CREATE OR REPLACE FUNCTION record_dept_changes()
RETURNS TRIGGER AS
$$
BEGIN
IF NEW.department <> OLD.department
THEN INSERT INTO employee_dept_changes(employee_no, name, department, changed_on)
VALUES(OLD.employee_no, OLD.name, OLD.department, now());
END IF;
RETURN NEW;
END;
$$
LANGUAGE 'plpgsql';
```

The preceding function inserts the old department along with the rest of the employee data into the `employee_dept_changes` table and adds the time of change if the employee's department changes.

The following example demonstrates how to bind the trigger function to the `employees` table:

```sql
CREATE TRIGGER dept_changes
BEFORE UPDATE ON employees
FOR EACH ROW
EXECUTE PROCEDURE record_dept_changes();
```

The trigger name in the preceding example is `dept_changes`. The trigger function is automatically invoked before the value of the `department` column is updated.

The following example updates the department for one of the employees from the `employees` table from Sales to Marketing:

```sql
UPDATE employees
SET department = 'Marketing'
WHERE employee_no = 1222;
```

The following is the output produced by the preceding example:

```output
employee_no | name                | department
------------+---------------------+------------------
1221        | John Smith          | Marketing
1222        | Bette Davis         | Marketing
1223        | Lucille Ball        | Operations
1224        | John Zimmerman      | Sales
```

The following example retrieves all data from the `employee_dept_changes` table:

```sql
SELECT * FROM employee_dept_changes;
```

The following is the output produced by the preceding example:

```output
employee_no | name            | department   | changed_on
------------+-----------------+--------------+----------------------------
1222        | Bette Davis     | Sales        | 2021-02-11 16:12:09.248823
```

The `employee_dept_changes` table is populated with a row containing the employee whose department has changed, as well as the date and time of the change.

## Delete triggers

The `DROP TRIGGER` statement allows you to delete the trigger from a table.

The `DROP TRIGGER` statement has the following syntax:

```sql
DROP TRIGGER [IF EXISTS] tr_name
  ON tbl_name [ CASCADE | RESTRICT ];
```

*tr_name* represents the trigger to be deleted if it exists. If you try to delete a non-existing trigger without using the `IF EXISTS` statement, the `DROP TRIGGER` statement results in an error, whereas using `IF EXISTS` to delete a non-existing trigger results in a notice. *tbl_name* represents the table associated with the trigger. The `CASCADE` option allows you to automatically delete objects that depend on the trigger and the `RESTRICT` option (default) allows you to refuse to delete the trigger if it has dependent objects.

### Example

The following example demonstrates how to delete the `dept_changes` trigger used in the examples from [Create triggers](#create-triggers):

```sql
DROP TRIGGER dept_changes ON employees;
```

## Enable and Disable triggers

You can disable one or more triggers associated with a table via the `ALTER TABLE DISABLE TRIGGER` statement that has the following syntax:

```sql
ALTER TABLE tbl_name
  DISABLE TRIGGER tr_name |  ALL;
```

*tbl_name* represents the table whose trigger represented by *tr_name* you are disabling. Using the `ALL` option allows you to disable all triggers associated with the table. A disabled trigger, even though it exists in the database, cannot fire on an event associated with this trigger.

### Examples

The following example shows how to disable a trigger on the `employees` table:

```sql
ALTER TABLE employees
  DISABLE TRIGGER dept_changes;
```

The following example shows how to disable all triggers associated with the `employees` table:

```sql
ALTER TABLE employees
  DISABLE TRIGGER ALL;
```

You can enable one or more previously disabled triggers associated with a table via the `ALTER TABLE ENABLE TRIGGER` statement that has the following syntax:

```sql
ALTER TABLE tbl_name
  ENABLE TRIGGER tr_name |  ALL;
```

*tbl_name* represents the table whose trigger represented by *tr_name* you are enabling. Using the `ALL` option allows you to enable all triggers associated with the table.

The following example shows how to enable a trigger on the `employees` table:

```sql
ALTER TABLE employees
  ENABLE TRIGGER dept_changes;
```

The following example shows how to enable all triggers associated with the `employees` table:

```sql
ALTER TABLE employees
  ENABLE TRIGGER ALL;
```

## Event triggers

The main difference between regular triggers and event triggers is that the former capture data manipulation events on a single table, whereas the latter can capture data definition events on a database.

The `CREATE EVENT TRIGGER` statement has the following syntax:

```sql
CREATE EVENT TRIGGER tr_name ON event
  [ WHEN filter_variable IN (filter_value [, ... ]) [ AND ... ] ]
  EXECUTE PROCEDURE function_name();
```

*tr_name*, which is unique in the database, represents the new trigger. *event* represents the event that triggers a call to the function  *function_name* whose return type is `event_trigger` (optional). You can define more than one trigger for the same event, in which case the triggers fire in alphabetical order based on the name of the trigger. If a `WHEN` condition is included in the `CREATE EVENT TRIGGER` statement, then the trigger is fired for specific commands. *filter_variable* needs to be set to`TAG`, as this is the only supported variable, and *filter_value* represents a list of values for *filter_variable*.

### Example

The following example is based on examples from [Create triggers](#create-triggers), except that the `record_dept_changes` function returns an event trigger instead of a regular trigger. The example shows how to create an  `sql_drop` trigger for one of the events currently supported by YSQL:

```sql
CREATE OR REPLACE FUNCTION record_dept_changes()
RETURNS EVENT_TRIGGER AS
$$
BEGIN
IF NEW.department <> OLD.department
THEN INSERT INTO employee_dept_changes(employee_no, name, department, changed_on)
VALUES(OLD.employee_no, OLD.name, OLD.department, now());
END IF;
END;
$$
LANGUAGE 'plpgsql';
```

```sql
CREATE EVENT TRIGGER dept_changes ON sql_drop
  EXECUTE PROCEDURE record_dept_changes();
```
