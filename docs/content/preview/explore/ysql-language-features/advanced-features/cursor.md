---
title: Cursors
linkTitle: Cursors
description: Using Cursors in YSQL
menu:
  preview:
    identifier: advanced-features-cursor
    parent: advanced-features
    weight: 200
aliases:
  - /preview/explore/ysql-language-features/cursor/
type: docs
---

A cursor represents a pointer with read-only access to the result set of a SELECT query. Cursors allow you to encapsulate a query and process individual rows. This enables you to divide a large result set into parts for processing of each part, thus making your query more efficient and preventing memory overflow.

You can create a function that returns a reference to a cursor, therefore returning a large result set ready to be processed by the caller of the function. You can also use cursors in procedures.

The following statements, in order, are associated with YSQL cursors:

- DECLARE – Allows you to create and execute the cursor inside a transaction.

- OPEN – Allows you to open the cursor.

- FETCH – Allows you to retrieve rows from an open cursor.

- CLOSE – Allows you to close the cursor and release memory used during the process.

{{<note>}}
Operations involving cursors must be performed inside transactions.
{{</note>}}

## Declare a cursor

You need to declare a cursor before you can open and use it. There are two ways to declare a cursor:

- As a variable of type `refcursor` placed in the YSQL block's declaration section, as demonstrated by the following syntax:

  ```sql
  DECLARE new_cursor refcursor;
  ```

- As an element bound to a query, based on the following syntax:

  ```sql
  new_cursor CURSOR [( arguments )] FOR a_query;
  ```

  *new_cursor* represents a variable name for the cursor. *arguments* represents a comma-separated list of query parameters that are substituted by values when the cursor is opened. *a_query* represents any SELECT statement.

The following example shows how to declare cursors for the `employees` table:

```sql
DECLARE
  employees_cursor_1 refcursor;
  employees_cursor_2 CURSOR FOR SELECT * FROM employees;
```

`employees_cursor_1` is not bound to a query so it can be used with any query, whereas `employees_cursor_2` encapsulates all rows in the `employees` table and is bound to s specific query.

## Open a cursor

The following example shows how to open an unbound cursor. As the cursor variable was not bounded to any query when the cursor was declared, you need to specify the query when you are opening the cursor.

```sql
OPEN employees_cursor_1 FOR SELECT * FROM employees;
```

The following example shows how to open a cursor that was already bound to a query at the time of its declaration. In this case, if there are any query parameters, you have to pass them.

```sql
OPEN employees_cursor_2;
```

## Use a cursor

You can use an open cursor with the YSQL data manipulation statements such as FETCH and DELETE.

### Fetch rows

Using the FETCH statement, you can obtain all rows or a specific row from the cursor and place it into a target such as a record, a row variable, or a comma-separated list of variables. When the rows are exhausted, the target is set to NULL.

The following examples show how to apply the FETCH statement to a cursor:

```sql
FETCH ALL FROM employees_cursor_2;
```

```sql
FETCH FROM employees_cursor_2 INTO employees_row;
```

### Delete rows

The following example shows how to delete all rows from a cursor:

```sql
DELETE FROM employees_cursor_2;
```

### Return a cursor

You can return a cursor using a function that opens the cursor and returns its name to the caller which, in turn, can fetch rows from the cursor and close it before the end of the transaction, if needed.

For more information and examples, refer to [Using Cursors](https://www.postgresql.org/docs/11/plpgsql-cursors.html#PLPGSQL-CURSOR-USING) in the PostgreSQL documentation.

### Use loops

You can iterate through the result set of a bound cursor using a certain form of the FOR statement, as per the following syntax:

```sql
FOR rec_var
IN bound_cursor_var [ ( [ argument_name := ] argument_value [, ...] ) ]
LOOP
  statements
END LOOP;
```

The cursor is automatically opened by the FOR statement and closed when the loop exits. If the cursor was declared to accept arguments, a list of argument value expressions must appear (they will be substituted in the query).

*rec_var* is always of type `record`. This variable's lifecycle is limited by the loop, with each row returned by the cursor assigned to it as the loop body is executed.

## Close a cursor

You use the CLOSE statement to complete the cursor lifecycle. By closing the cursor, you release resources before the end of the transaction. This also releases the cursor variable which allows you to open the cursor again.

The following example shows how to close a cursor:

```sql
CLOSE employees_cursor_2;
```

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

The following example shows how to declare a cursor and use it to retrieve all rows from the `employees` table:

```sql
BEGIN;
  DECLARE employees_cursor_2 CURSOR FOR SELECT * FROM employees;
  FETCH ALL FROM employees_cursor_2;
END;
```

The following is the output produced by the preceding example:

```output
 employee_no | name         | department
-------------+--------------+---------------
 1221        | John Smith   | Marketing
 1222        | Bette Davis  | Sales
 1223        | Lucille Ball | Operations
```
