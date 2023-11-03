---
title: Queries and joins
linkTitle: Queries and joins
description: Queries and joins in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.16:
    identifier: explore-ysql-language-features-queries-joins
    parent: explore-ysql-language-features
    weight: 210
type: docs
---

This section describes how to query YugabyteDB using the YSQL `SELECT` statement and its clauses.

## Query data

The main purpose of `SELECT` statements is to retrieve data from specified tables. Typically, the first part of every `SELECT` statement defines columns that contain the required data, the second part points to the tables hosting these columns, and the third, optional part, lists restrictions.

`SELECT` has the following syntax:

```sql
SELECT list FROM table_name;
```

*list* represents a column or a list of columns in a *table_name* table that is the subject of data retrieval. If you provide a list of columns, you need to use a comma separator between two columns. To select data from all the columns in the table, you can use an asterisk. *list* can also contain expressions or literal values.

Note that the `FROM` clause is evaluated before `SELECT`.

The following `SELECT` statement clauses provide flexibility and allow you to fine-tune queries:

- The `DISTINCT` operator allows you to select distinct rows.
- The `ORDER BY` clause lets you sort rows.
- The `WHERE` clause allows you to apply filters to rows.
- The `LIMIT` clause allows you to select a subset of rows from a table.
- The `GROUP BY` clause allows you to divide rows into groups.
- The `HAVING` clause lets you filter groups.
- The `INNER JOIN`, `LEFT JOIN`, `FULL OUTER JOIN`, and `CROSS JOIN` clauses let you create joins with other tables.

### SELECT examples

{{% explore-setup-single %}}

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

You can use the `SELECT` statement to find names of all employees in the `employees` table. In this case, you apply `SELECT` to one column only, as follows:

```sql
SELECT name FROM employees;
```

To retrieve data that includes the employee name and department, you need to query multiple columns, as shown in the following example:

```sql
SELECT name, department FROM employees;
```

To obtain data from all the columns in the table, you can use an asterisk, as shown in the following example:

```sql
SELECT * FROM employees;
```

You can also use expressions for data retrieval via `SELECT`. The following example shows how to use the concatenation operator to get employee numbers and names combined with departments for all employees:

```sql
SELECT employee_no, name || ' ' || department FROM employees;
```

The following is the output produced by the preceding example:

```output
employee_no | ?column
------------+---------------------------
1221        | John Smith Marketing
1222        | Bette Davis Sales
1223        | Lucille Ball Operations
1224        | John Zimmerman Sales
```

In some cases, you may omit the `FROM` clause in the `SELECT` statement. The following example shows the `SELECT` statement that uses and expression to perform multiplication and outputs the result in a column:

```sql
SELECT 2 * 5;
```

You can always view your table definitions by executing the following command:

```shell
yugabyte=# \d employees
```

### Column aliases

You can use YSQL column aliases to provide meaningful column headers to a query output by assigning a temporary name to a column or an expression in the select list of your `SELECT` statement. An alias lifecycle ends as soon as the query finished executing.

A column alias has the following syntax:

```sql
SELECT column_name AS alias_name FROM table_name;
```

The `AS` keyword is optional; if omitted, the following syntax applies:

```sql
SELECT column_name alias_name FROM table_name;
```

The following syntax is used for setting an alias for an expression:

```sql
SELECT expression AS alias_name FROM table_name;
```

Using the table from [SELECT examples](#select-examples), the following example demonstrates how to retrieve data that includes the employee name and department:

```sql
SELECT name, department FROM employees;
```

The following example renames the `department` column to `section` using an alias:

```sql
SELECT name, department AS section FROM employees;
```

The following is the output produced by the preceding example:

```output
name                | section
--------------------+---------------------------
John Smith          | Marketing
Bette Davis         | Sales
Lucille Ball        | Operations
John Zimmerman      | Sales
```

Column aliases may contain spaces. In this case, you enclose them in double quotes to produce multi-word headers, as shown in the following example:

```sql
SELECT name, department AS "section of the company" FROM employees;
```

### Sort and order data

The `SELECT` statement returns data in an unspecified order. You can use the `SELECT` statement's `ORDER BY` clause to sort the rows of the query result set in ascending or descending order based on a sort expression.

`ORDER BY` has the following syntax:

```sql
SELECT list
  FROM table_name
  ORDER BY sort_expression1 [ASC | DESC] [NULLS FIRST | NULLS LAST],
  ...,
  sort_expressionN [ASC | DESC];
```

*sort_expression* can be a column name or an expression that you intend to sort. To sort the query result set based on multiple columns or expressions, include a comma separator between two columns or expressions. Use the `ASC` option to sort rows in ascending order and `DESC` to sort rows in descending order; if you do not specify these options, `ASC` is used by default.

The `ORDER BY` clause is evaluated after `FROM` and `SELECT`. This gives you an opportunity to specify a column alias in the `SELECT` statement and use this alias in the `ORDER BY` clause.

Using the table from [SELECT examples](#select-examples), the following example demonstrates how sort employees based on their name in ascending order:

```sql
SELECT name, department FROM employees ORDER BY name DESC;
```

The following is the output produced by the preceding example:

```output
name                | department
--------------------+---------------------------
Lucille Ball        | Operations
John Smith          | Marketing
John Zimmerman      | Sales
Bette Davis         | Sales
```

Omitting the `DESC` option would result in the employees sorted in ascending order by their name.

The following example selects the name and department from the `employees` table, then sorts the rows by the name in ascending order, and then sorts the already sorted rows by department in descending order:

```sql
SELECT name, department FROM employees
  ORDER BY name ASC, department DESC;
```

The following is the output produced by the preceding example:

```output
name                | department
--------------------+---------------------------
Bette Davis         | Sales
John Zimmerman      | Sales
John Smith          | Marketing
Lucille Ball        | Operations
```

Sorting rows that contain `NULL`  is typically done by using the `ORDER BY` clause's `NULLS FIRST` and `NULLS LAST` options. This allows you to specify the order of `NULL` with other non-null values: `NULLS FIRST` places `NULL` before other non-null values, whereas `NULL LAST` places `NULL` after other non-null values.

Which `NULL` option of the `ORDER BY` clause is used by default depends on whether a `DESC` or `ASC` option is specified: the `ORDER BY` clause with the `DESC` option uses the `NULLS FIRST` by default, and the `ORDER BY` clause with the `ASC` option uses the `NULLS LAST` by default.

The following example demonstrates how to sort the `employees` table by department in ascending order displaying rows with missing departments first:

```sql
SELECT department FROM employees
  ORDER BY department ASC NULLS FIRST;
```

### Duplicate rows

You can use the `DISTINCT` clause in the `SELECT` statement to remove duplicate rows from a query result. The `DISTINCT` clause keeps one row for each set of duplicates. You can apply this clause to columns included in the `SELECT` statement's select list.

The `DISTINCT` clause has the following syntax, with values in `column_name` evaluating the duplicate:

```sql
SELECT DISTINCT column_name FROM table_name;
```

In cases when multiple columns are used, the `DISTINCT` clause combines values of these columns to evaluate the duplicate.

Because the order of rows returned by the `SELECT` statement is unspecified, the first row of each set of duplicates is unknown. `DISTINCT ON (expression)` allows you to keep the first row of each set of duplicates. using the following syntax:

The `DISTINCT ON (expression)` clause has the following syntax:

```sql
SELECT DISTINCT ON (column_name_1) column_alias, column_name_2
  FROM table_name
  ORDER BY column_name_1, column_name_2;
```

The following series of examples inserts new rows into the table from [SELECT examples](#select-examples), then queries the `employees` table using `SELECT` with its `DISTINCT` option enabled, thus removing duplicate values, and then sorts the result set in descending order based on the employee name:

```sql
INSERT INTO employees (employee_no, name, department)
VALUES
(9, 'Jean Harlow', 'Sales'),
(8, 'Jean Harlow', 'Sales');
```

```sql
SELECT DISTINCT name FROM employees ORDER BY name DESC;
```

The following is the output produced by the preceding examples:

```output
name
--------------------
Lucille Ball
John Smith
John Zimmerman
Jean Harlow
Bette Davis
```

### Case sensitivity

YSQL converts identifiers to lowercase unless they are enclosed in quotation marks. That is, YSQL is case-insensitive for all practical purposes by default. For example, a table called `Employees` would be recognized as the `employees` table and the following query would be executed on the `employees` table without any problems:

```sql
SELECT name FROM Employees;
```

The following example shows how to run a query specifically on a table called `Employees`:

```sql
SELECT name FROM "Employees";
```

## Filter data

The `WHERE` clause allows you to filter data returned by the `SELECT` statement. Only the rows that satisfy a specified condition are included in the result set.

The `WHERE` clause has the following syntax:

```sql
SELECT list FROM table_name
  WHERE condition
  ORDER BY expression;
```

*condition* is a boolean expression or a combination of boolean expressions created with `AND` and `OR` logical operators. *condition* evaluates to `TRUE`, `FALSE`, or unknown. The result set only returns rows that cause *condition* to evaluate to `TRUE`.

The following example uses the table from [SELECT examples](#select-examples) to demonstrate how to use the `AND` operator to combine two Boolean expressions in order to find an employee number of a specific employee working for a specified department:

```sql
SELECT employee_no FROM employees
  WHERE name = 'John Smith' AND department = 'Marketing';
```

The following is the output produced by the preceding example:

```output
employee_no
--------------------
1221
```

The following example shows how to use the `OR` operator to find employee IDs of specific employees:

```sql
SELECT employee_no FROM employees
  WHERE name = 'John Smith' OR name = 'Bette Davis';
```

The following is the output produced by the preceding example:

```output
employee_no
--------------------
1221
1222
```

During the query execution, the `WHERE` clause is evaluated after the `FROM` clause but before the `SELECT` and `ORDER BY` clause.

You cannot use column aliases in the `WHERE` clause of  `SELECT`.

You can define the condition in the `WHERE` clause by using any standard SQL comparison operators and almost all logical operators except `ALL`, `ANY`, and `SOME`.

To find a string that matches a specified pattern, you use the `LIKE` operator. The following example returns all customers whose first names start with the string `Ann`:

The following example shows how to use the `LIKE` operator to find a string that matches a specified pattern returning employees:

```sql
SELECT name, department FROM employees WHERE name LIKE 'John%';
```

The following is the output produced by the preceding example:

```output
name            | department
----------------+--------------------
John Smith      | Sales
John Zimmerman  | Marketing
```

YSQL also allows you to use numeric expressions and dates in the `WHERE` clause, as shown in the following examples that use the table from [SELECT examples](#select-examples):

```sql
SELECT name FROM employees WHERE employee_no = 1000 + 222;
```

The following is the output produced by the preceding example:

```output
name
--------------------
Bette Davis
```

If the `employees` table had an additional column for the current timestamp that records the time of changes to every row, then the following example could have demonstrated how to use date expressions in the `WHERE` clause:

```sql
SELECT name FROM employees
  WHERE CURRENT_TIMESTAMP = '2021-01-03 16:22:29.079+07:30';
```

If one of the employee records was last modified on specific date and time, then the following could be the output produced by the preceding example:

```output
name
--------------------
John Smith
```

### LIMIT clause

The `LIMIT` clause of the `SELECT` statement allows you to impose constrains on the number of rows that your query can return.

The `LIMIT` clause has the following syntax:

```sql
SELECT list FROM table_name
  ORDER BY expression
  LIMIT rows_number;
```

*rows_number* represents the number of rows included in the query result set. If this number is set to zero, the query does not return any rows. If *rows_number* is set to `NULL`, the query result is the same as if the `SELECT` statement did not contain the `LIMIT` clause.

To skip rows before returning the rows specified by *rows_number*, you can use the `OFFSET` clause immediately after the `LIMIT` clause, as per the following syntax:

```sql
SELECT list FROM table_name
  LIMIT rows_number OFFSET row_skip;
```

*row_skip* represents the number of rows that the query skips before returning *rows_number* rows. If *row_skip* is set to zero, the query result is the same as if the `SELECT` statement did not contain the `OFFSET` clause.

Because rows are often stored in tables in an unspecified order, it is recommended that you include the `ORDER BY` clause in `SELECT` statements that contain the `LIMIT` clause.

Using the table from [SELECT examples](#select-examples), the following example demonstrates how retrieve the first two employees sorted by their number:

```sql
SELECT employee_no, name FROM employees
  ORDER BY employee_no
  LIMIT 2;
```

The following is the output produced by the preceding example:

```output
employee_no | name
------------+--------------------
1221        | John Smith
1222        | Bette Davis
```

The following example demonstrates how retrieve three employees starting from the second one ordered by their number:

```sql
SELECT name, department FROM employees
  ORDER BY name
  LIMIT 3
  OFFSET 2;
```

The following is the output produced by the preceding example:

```output
name                | department
--------------------+----------------------
John Smith          | Marketing
John Zimmerman      | Sales
Lucille Ball        | Operations
```

### LIKE operator

There are cases when you do not know the exact query parameter but have an idea of a partial parameter. Using the `LIKE` operator allows you to match this partial information with existing data based on a pattern recognition.

The following is the syntax of the `LIKE` operator:

```sql
value LIKE pattern
```

The expression evaluates to `true` if *value* matches *pattern*.

For example, if your goal is to find an employee and their department, yet you only know that the employee name starts with "Luci", you can execute the following query on a table created in [SELECT examples](#select-examples):

```sql
SELECT name, department FROM employees WHERE name LIKE 'Luci%';
```

The following is the output produced by the preceding example:

```output
name          | department
--------------+----------------
Lucille Ball  | Operations
```

The `WHERE` clause contains a special expression consisting of `name`, the `LIKE` operator, and a string that contains a percent sign. The string `'Luci%'` represents a pattern.

Rows returned by the query are those whose values in the `name` column begin with `Luci` and might be followed by any other characters.

To construct a pattern, you combine literal values with wildcard characters such as percent sign or underscore and use the `LIKE` or `NOT LIKE` operator to search for matches. Percent sign enables you to find a match for any sequence of any number of characters, whereas underscore matches any single character.

If you do not provide a wildcard character in the pattern, the `LIKE` operator acts like the equal operator.

The following is the syntax of the `NOT LIKE` operator:

```sql
value NOT LIKE pattern
```

The `NOT LIKE` operator behaves as an opposite of the `LIKE` operator and returns `true` when *value* does not match the *pattern*.

## Group data

YSQL allows you to divide rows of the result set into groups using the `GROUP BY` clause of the `SELECT` statement. You can apply an aggregate function to each group to calculate the sum of items. You can also count items in a group using the `COUNT()` function.

The `GROUP BY` clause has the following syntax:

```sql
SELECT column_1, column_2, aggregate_function(column_3)
  FROM table_name
  GROUP BY column_1, column_2;
```

*column_1* and *column_2* in the `SELECT` part of the statement represent columns that are to be selected. *column_3* represents a column to which an aggregate function is applied. In the second, `GROUP BY` part of the statement, *column_1* and *column_2* are the columns that you want to group.

The purpose of the statement clause is to divide the rows by the values of the columns specified in the `GROUP BY` clause and calculate a value for each group.

The `GROUP BY` clause is evaluated after the `FROM` and `WHERE` clauses but before the `HAVING`, `SELECT`, `DISTINCT`, `ORDER BY`, and `LIMIT` clauses.

Using the table from [SELECT examples](#select-examples), the following example demonstrates how retrieve data from a table and group the result by `employee_no`:

```sql
SELECT employee_no FROM employees GROUP BY employee_no;
```

If there were duplicate rows in the result set from the preceding example, these rows would have been removed because the `GROUP BY` clause functions like the `DISTINCT` clause in this case.

If the `employees` table had an `amount` column with the employee pay data, the following example would have demonstrated how to select the total amount that each employee was paid. The `GROUP BY` clause would have divided the rows in the `employees` table into groups by employee number. For each group, the total amounts would have been calculated using the `SUM()` function:

```sql
SELECT employee_no, SUM (amount) FROM employees GROUP BY employee_no;
```

### HAVING clause

To define search condition for a group or an aggregate, you can use the `HAVING` clause. If you use this clause in combination with the `GROUP BY` clause, you can filter groups and aggregates based on the condition.

The `HAVING` clause has the following syntax:

```sql
SELECT column_1, aggregate_function(column_2)
  FROM table_name
  GROUP BY column_1
  HAVING condition;
```

The `GROUP BY` clause returns rows grouped by *column_1*.  The `HAVING` clause specifies *condition* to filter the groups.

The `HAVING` clause is evaluated after the `FROM`, `WHERE`, `GROUP BY`, but before the `SELECT`, `DISTINCT`, `ORDER BY`, and `LIMIT` clauses. Because the `HAVING` clause is evaluated before the `SELECT` clause, you cannot use column aliases in the `HAVING` clause.

Using the table from [SELECT examples](#select-examples), the following example demonstrates how to select the department that has more than one employee:

```sql
SELECT department, COUNT (employee_no)
  FROM employees
  GROUP BY department
  HAVING COUNT (employee_no) > 1;
```

The following is the output produced by the preceding examples:

```output
department  | count
------------+----------
Sales       | 2
```

## Join columns

You can combine (join) columns from the same or different tables based on the values of the common columns between related tables. Typically, common columns contain primary keys in the first table and foreign key in the second table.

Cross join, inner join, right outer join, left outer join, and full outer join are all supported by YSQL.

Suppose you work with a database that includes two tables created and populated as follows:

```sql
CREATE TABLE fulltime_employees (
  ft_employee_no integer PRIMARY KEY,
  ft_name text,
  ft_department text
);
```

```sql
INSERT INTO fulltime_employees VALUES
(1221, 'John Smith', 'Marketing'),
(1222, 'Bette Davis', 'Sales'),
(1223, 'Lucille Ball', 'Operations'),
(1224, 'John Zimmerman', 'Sales');
```

```sql
CREATE TABLE permanent_employees (
  perm_employee_no integer  PRIMARY KEY,
  perm_name text,
  perm_department text
);
```

```sql
INSERT INTO permanent_employees VALUES
(1221, 'Lucille Ball', 'Operations'),
(1222, 'Cary Grant', 'Operations'),
(1223, 'John Smith', 'Marketing');
```

The following is the `fulltime_employees` table:

```output
ft_employee_no  | ft_name           | ft_department
----------------+-------------------+-------------------
1221            | John Smith        | Marketing
1222            | Bette Davis       | Sales
1223            | Lucille Ball      | Operations
1224            | John Zimmerman    | Sales
```

The following is the `permanent_employees` table:

```output
perm_employee_no  | perm_name           | perm_department
------------------+---------------------+----------------------
1221              | Lucille Ball        | Operations
1222              | Cary Grant          | Operations
1223              | John Smith          | Marketing
```

You can always view your table definitions by executing the following commands:

```shell
yugabyte=# \d fulltime_employees
yugabyte=# \d permanent_employees
```

### Inner join

The following example demonstrates how to join the `fulltime_employees` table with the `permanent_employees` table by matching the values in the `ft_name` and `perm_name` columns:

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  INNER JOIN permanent_employees
  ON ft_name = perm_name;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name       | perm_employee_no | perm_name
----------------+---------------+------------------+----------------
1221            | John Smith    | 1223             | John Smith
1223            | Lucille Ball  | 1221             | Lucille Ball
```

Each row of the `fulltime_employees` table has been examined and the value in its `ft_name` column compared with the value in the `perm_name` column for each row in the `permanent_employees` table. In case of equal values, a new row was created and its columns populated by values from both tables, then this new row was added to the result set.

### Left outer join

The following example demonstrates how to use the left join to join the `fulltime_employees` table (left table) with the `permanent_employees` table (right table):

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  LEFT JOIN permanent_employees
  ON ft_name = perm_name;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name         | perm_employee_no  | perm_name
----------------+-----------------+-------------------+-----------------
1221            | John Smith      | 1223              | John Smith
1223            | Lucille Ball    | 1221              | Lucille Ball
1222            | Bette Davis     | [null]            | [null]
1224            | John Zimmerman  | [null]            | [null]
```

The statement execution starts by selecting data from the  `fulltime_employees`  table, values in its `ft_name` column are compared with the values in the `perm_name` column for each row in the `permanent_employees` table. In case of equal values, a new row is created and its columns populated by values from both tables, then this new row is added to the result set. When non-equal values are encountered, a new row is created containing columns from both tables, and then this new row is added to the result set. The columns of the right table `permanent_employees`  are populated with `null` values.

The following example shows how to select the `fulltime_employees` table rows that do not have matching rows in the `permanent_employees` table:

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  LEFT JOIN permanent_employees
  ON ft_name = perm_name
  WHERE perm_employee_no IS NULL;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name         | perm_employee_no  | perm_name
----------------+-----------------+-------------------+----------------
1222            | Bette Davis     | [null]            | [null]
1224            | John Zimmerman  | [null]            | [null]
```

### Right outer join

Unlike the left join that starts data selection from the left table, the right join starts selecting data from the right table. It compares every value in the `perm_name` column of every row in the `permanent_employees` table (right table) with every value in the `ft_name` column of every row in the `fulltime_employees` table (left table). In case of equal values, a new row that contains columns from both tables is created. When non-equal values are encountered, an additional new row containing columns from both tables is created and columns of the left table `fulltime_employees` is populated with `null` values.

The following example demonstrates how to use the right join to join the `fulltime_employees` table with the `permanent_employees` table:

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  RIGHT JOIN permanent_employees
  ON ft_name = perm_name;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name         | perm_employee_no  | perm_name
----------------+-----------------+-------------------+------------------
1223            | John Smith      | 1221              | John Smith
1221            | Lucille Ball    | 1223              | Lucille Ball
[null]          | [null]          | 1222              | Cary Grant
```

By adding a `WHERE` clause to the end of the `SELECT` statement, you can obtain rows from the right table that do not have matching rows in the left table.

### Full outer join

The full outer join allows you to obtain a result set that contains all rows from left and right table, with the matching rows from both sides (if any). If no match exists, as in the following example, the left table's columns are populated with `null` values:

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  FULL OUTER JOIN permanent_employees
  ON ft_name = perm_name;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name         | perm_employee_no  | perm_name
----------------+-----------------+-------------------+-----------------
1221            | John Smith      | 1223              | John Smith
1222            | Bette Davis     | 1221              | Lucille Ball
1223            | Lucille Ball    | [null]            | [null]
1224            | John Zimmerman  | [null]            | [null]
[null]          | [null]          | 1222              | Cary Grant
```

The following example shows how to  use the full join with a `WHERE` clause to return rows in a table that do not have matching rows in another table:

```sql
SELECT ft_employee_no, ft_name, perm_employee_no, perm_name
  FROM fulltime_employees
  FULL JOIN permanent_employees
  ON ft_name = perm_name
  WHERE ft_employee_no IS NULL OR perm_employee_no IS NULL;
```

The following is the output produced by the preceding examples:

```output
ft_employee_no  | ft_name         | perm_employee_no  | perm_name
----------------+-----------------+-------------------+----------------
1222            | Bette Davis     | [null]            | [null]
1224            | John Zimmerman  | [null]            | [null]
[null]          | [null]          | 1222              | Cary Grant
```

### Cross join

You can use a cross join to generate a Cartesian product of rows in at least two tables.

Unlike other join clauses, the `CROSS JOIN` clause does not have a join predicate.

The `CROSS JOIN` clause has the following syntax:

```sql
SELECT list FROM table_1_name
  CROSS JOIN table_2_name;
```

You may omit the `CROSS JOIN` clause and use the following syntax instead:

```sql
SELECT list FROM table_1_name, table_2_name;
```

As an alternative, you can use the following syntax to simulate the cross join by using an inner join with a condition which always evaluates to `true`:

```sql
SELECT * FROM table_1_name
  INNER JOIN table_2_name ON true;
```

The `fulltime_employees` table has 4 rows and the `permanent_employees` table has 3 rows. When these tables are cross-joined, the result set has 4 * 3 = 12 rows, as the following example demonstrates:

```sql
SELECT * FROM fulltime_employees
  CROSS JOIN permanent_employees;
```

## Subqueries

Subqueries allow you to construct complex queries by executing `SELECT`, `INSERT`, `DELETE`, or `UPDATE` statements from within other such statements.

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  years_service numeric
);
```

```sql
INSERT INTO employees (employee_no, name, department, years_service)
VALUES
  (1221, 'John Smith', 'Marketing', 5),
  (1222, 'Bette Davis', 'Sales', 3),
  (1223, 'Lucille Ball', 'Operations', 1),
  (1224, 'John Zimmerman', 'Sales', 5);
```

If you need to find the employees who have been working for the company longer than average, you start by calculating the average years of service using a `SELECT` statement and average function `AVG`. Then you use the result of the first query in the second `SELECT` statement to find the long-serving employees, as shown in the following examples:

```sql
SELECT AVG (years_service) FROM employees;
```

The preceding query returns 3.5000000000000000.

```sql
SELECT employee_no, name, years_service FROM employees
  WHERE years_service > 3.5;
```

The following is the output produced by the preceding example:

```output
employee_no | name             | years_service
------------+------------------+-------------------
1221        | John Smith       | 5
1224        | John Zimmerman   | 5
```

You can avoid executing two separate queries by using a subquery that is passed the result of the first query. To create such a query, you enclose the second query in brackets and use it as an expression in the `WHERE` clause, as shown in the following example:

```sql
SELECT employee_no, name, years_service FROM employees
  WHERE years_service > (SELECT AVG (years_service) FROM employees);
```

YSQL executes the subquery first, obtains the result and passes it to the outer query, and finally  executes the outer query.

You can use a subquery that is an input of the `EXISTS` operator which returns true if the subquery returns any rows. The `EXISTS` operator returns false in cases where the subquery does not return any rows. The `EXISTS` operator does not access the content of the rows; it only needs to know the number of rows returned by the subquery.

The `EXISTS` operator has the following syntax:

```sql
EXISTS (SELECT 1 FROM some_table WHERE condition);
```

The following example applies the `EXISTS` operator to two tables (the  `employees` table and a hypothetical  `salary` table) and shows how to use `EXISTS` on the `employee_no` column in a manner similar to an inner join:

```sql
SELECT name FROM employees
  WHERE EXISTS (SELECT 1 FROM salary
                  WHERE salary.employee_no = employees.employee_no);
```

If the `salary` table existed, the preceding query would have returned no more than one row for each row in the `employees` table, even though there would have been corresponding rows in the `salary` table.

## Recursive queries and CTEs

Common Table Expressions (CTEs) allow you to execute recursive, hierarchical, and other types of complex queries in a simplified manner by breaking down these queries into smaller units. A CTE exists only during the query execution and represents a temporary result set that you can reference from another SQL statement.

You can use the following syntax to create a basic CTE:

```sql
WITH cte_name (columns) AS (cte_query) statement;
```

*cte_name* represents the name of the CTE. *columns* is an optional list of table columns. *cte_query* represents a query returning a result set. If *columns* is not specified, the select list of the *cte_query* becomes *columns* of the CTE. *statement* can be a `SELECT`, `INSERT`, `UPDATE`, or `DELETE` YSQL statement, and the CTE acts the way a table does in that statement.

Using the `fulltime_employees` table from [Join columns](#join-columns), the following example demonstrates how to define a CTE and use it to create a complex query:

```sql
WITH cte_fulltime_employees AS (
  SELECT
    ft_name,
      (CASE
         WHEN ft_employee_no < 1222 THEN 'Sales'
         WHEN ft_employee_no > 1223 THEN 'Marketing'
         ELSE 'Operations'
       END) ft_department
  FROM
    fulltime_employees
)
SELECT ft_name, ft_department
FROM cte_fulltime_employees
WHERE ft_department = 'Operations'
ORDER BY ft_name;
```

The following is the output produced by the preceding example:

```output
 ft_name             | ft_department
---------------------+-------------------
 Bette Davis         | Operations
 Lucille Ball        | Operations
```

Recursive queries, often used for querying hierarchical data, refer to recursive CTEs.

You can use the following syntax to create a recursive CTE:

```sql
WITH RECURSIVE cte_name
  AS(cte_query --- non-recursive
      UNION [ALL]
      cte_query_definition definion  --- recursive
  )
SELECT * FROM cte_name;
```

*cte_name* represents the name of the CTE. *cte_query* represents a non-recursive term which is a CTE query definition creating the base result set of the CTE structure. *cte_query_definition* represents a recursive term which is one or more CTE query definitions joined with the non-recursive term via the `UNION` or `UNION ALL` operator; the CTE name is referenced by the recursive term. The recursion terminates when the previous iteration does not return any rows.

Suppose you work with a database that includes the `employees` table created and populated as follows:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  manager_no integer,
  department text
);
```

```sql
INSERT INTO employees VALUES
  (1221, 'John Smith', NULL, NULL),
  (1222, 'Bette Davis', 1221, 'Sales'),
  (1223, 'Lucille Ball', 1221, 'Operations'),
  (1224, 'John Zimmerman', 1222, 'Sales'),
  (1225, 'Walter Marx', 1222, 'Sales');
```

The following example demonstrates how to retrieve all employees who report to the manager with `manager_no` 1222:

```sql
WITH RECURSIVE reports AS (
  SELECT employee_no, name, manager_no, department
  FROM employees
  WHERE employee_no = 1222
  UNION
    SELECT
      emp.employee_no, emp.name, emp.manager_no, emp.department
    FROM employees emp
    INNER JOIN reports rep ON rep.employee_no = emp.manager_no
)
SELECT * FROM reports;
```

In the preceding example, the recursive CTE `reports` defines a non-recursive and a recursive term, with non-recursive term returning the base result set that includes the employee whose `employee_no` is 1222. Then the recursive term retrieves the direct reports of the employee whose `employee_no` is 1222. This is the result of joining the `employees` table and the `reports` CTE. The first iteration of the recursive term returns two employees with  `employee_no` 1224 and 1225. The recursive term is executed multiple times. The second iteration uses the preceding result set as the input value but does not return any rows because nobody reports to employees with `employee_no` 1224 and 1225.

When the final result set is returned, it represents the combination of all result sets in iterations generated by the non-recursive  and recursive terms, as demonstrated by the following output:

```output
employee_no | name            | manager_id  | department
------------+-----------------+-------------+--------------
1222        | Bette Davis     | 1221        | Sales
1224        | John Zimmerman  | 1222        | Sales
1225        | Walter Marx     | 1222        | Sales
```

Another way to execute complex hierarchical queries is to use a `tablefunc` extension. This extension provides several table functions, such as, for example, `normal_rand()` that creates values picked using a pseudorandom generator from an ideal normal distribution. For more information and examples, see [tablefunc](../../../explore/ysql-language-features/pg-extensions/#tablefunc-example).
