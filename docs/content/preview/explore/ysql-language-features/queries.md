---
title: Reading data
linkTitle: Read data
description: Queries and joins to read data in YSQL
headcontent: Learn how to interact with your data using select, use joins and filters
menu:
  preview:
    identifier: explore-ysql-language-features-queries-joins
    parent: explore-ysql-language-features
    weight: 400
type: docs
---

Querying is the process of retrieving specific information from a database. This page provides an in-depth guide on how to query data from a database, starting with basic retrieval techniques and expanding into more advanced concepts like filtering, sorting, joining tables, and aggregating results. Whether you're a beginner learning how to extract information or an advanced user optimizing queries for performance, this guide will help you efficiently interact with your data.

## Select statement

The SELECT statement is used to retrieve data from tables. It begins by specifying the columns to be fetched, followed by the tables from which to retrieve the data. Additionally, it may include optional conditions for filtering the results and specifying the order in which the data should be returned. Let us explore this further with few examples.

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

## Sample schema

For the pupose of illustration, lets consider the following table and corresponding data.

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

## All data

To retrieve all the data from the table, you can use the following query.

```sql
SELECT * FROM employees;
```

You should see the following output.

```caddyfile{.nocopy}
 employee_no |      name      | department
-------------+----------------+------------
        1223 | Lucille Ball   | Operations
        1224 | John Zimmerman | Sales
        1221 | John Smith     | Marketing
        1222 | Bette Davis    | Sales
```

## Just one column

Let's say you want to fetch just the name of the all the employees. For this, you can run the following query.

```sql
SELECT name FROM employees;
```

You will get the following output.

```caddyfile{.nocopy}
      name
----------------
 Lucille Ball
 John Zimmerman
 John Smith
 Bette Davis
```

## Multiple columns

Let's say you want to fetch both the name and department of the all the employees. For this, you can run the following query.

```sql
SELECT name, department FROM employees;
```

You will get the following output.

```caddyfile{.nocopy}
      name      | department
----------------+------------
 Lucille Ball   | Operations
 John Zimmerman | Sales
 John Smith     | Marketing
 Bette Davis    | Sales
```

## Column aliases

Let's say you want to fetch both the name and department together as a single value of the all the employees. Because there is no defined name for the combined value, by default, the system will generate a column name as `?column?`. You can use the `AS` clause to assign a name for this generated column.

```sql
SELECT employee_no, name || ' - ' || department AS combined FROM employees;
```

You will get the following output.

```caddyfile{.nocopy}
 employee_no |         combined
-------------+---------------------------
        1223 | Lucille Ball - Operations
        1224 | John Zimmerman - Sales
        1221 | John Smith - Marketing
        1222 | Bette Davis - Sales
```

{{<tip>}}
Column aliases may contain spaces. In such a case, you need enclose them in double quotes to produce multi-word headers.
{{</tip>}}

## Ordering data

You can use the `ORDER BY` clause to order/sort your result set on a specific condition. You can also specify the kind of ordering you need( e.g, `ASC` or `DESC`). For example, to select few columns and order the results by the name of the employee, you can run

```sql
SELECT name, department FROM employees ORDER BY name DESC;
```

You will get the following output.

```caddyfile{.nocopy}
      name      | department
----------------+------------
 Lucille Ball   | Operations
 John Zimmerman | Sales
 John Smith     | Marketing
 Bette Davis    | Sales
```

{{<tip>}}
The default sort order of `ASC` will be used when not specified explicitly.
{{</tip>}}

You can also specify deifferent sort orders for different columns. For example, fetch the data sorted by the department in ascending order, and then sorting the rows with the same department by name in descending order, you can run:

```sql
SELECT name, department FROM employees
  ORDER BY department ASC, name DESC;
```

You will get the following output.

```caddyfile{.nocopy}
      name      | department
----------------+------------
 John Smith     | Marketing
 Lucille Ball   | Operations
 John Zimmerman | Sales
 Bette Davis    | Sales
```

## Ordering NULLs

Sorting rows that contain NULL values is usually done using the ORDER BY clause with the NULLS FIRST and NULLS LAST options. These options allow you to control the placement of NULL values relative to non-null values: NULLS FIRST positions NULL before non-null values, while NULLS LAST positions NULL after non-null values.

The default behavior for sorting NULL values depends on whether the DESC or ASC option is used in the ORDER BY clause. When using DESC, the default is NULLS FIRST, and with ASC, the default is NULLS LAST.

For example to order results by department in ascending order displaying rows with missing departments first, you can run:

```sql
SELECT department FROM employees
  ORDER BY department ASC NULLS FIRST;
```

## Duplicate rows

You can use the `DISTINCT` clause to remove duplicate rows from a query result. The `DISTINCT` clause keeps one row for each set of duplicates. You can apply this clause to columns included in the `SELECT` statement's select list. For example, to get the list of departments, you can run:

```sql
SELECT DISTINCT department FROM employees;
```

You will get the following output. Note that even though there are 2 employees in Sales, only one row for Sales has been returned. This is the effect of `DISTINCT`

```caddyfile{.nocopy}
 department
------------
 Marketing
 Operations
 Sales
```

## Filtering

The `WHERE` clause allows you to filter the results based on a coondition. Only the rows that satisfy a specified condition are included in the result set.

For example, to fetch rows only from the `Marketing` department, you can add a condition `department = 'Marketing'` in the `where` clause like:

```sql
SELECT * FROM employees WHERE department = 'Marketing';
```

The following is the output produced by the preceding example:

```caddyfile{.nocopy}
 employee_no |    name    | department
-------------+------------+------------
        1221 | John Smith | Marketing
```

You can use any of the supported [operators and expressions](../expressions-operators/) (except `ALL`, `ANY`, and `SOME`) to combine multiple conditions to fetch the results you need.

For example to fetch all employees with names starting with either `B` or `L` you can use the `OR` operator.

```sql
SELECT * FROM employees WHERE name LIKE 'B%' OR name LIKE 'L%';
```

The following is the output produced by the preceding example:

```caddyfile{.nocopy}
 employee_no |     name     | department
-------------+--------------+------------
        1223 | Lucille Ball | Operations
        1222 | Bette Davis  | Sales
```

During the query execution, the `WHERE` clause is evaluated after the `FROM` clause but before the `SELECT` and `ORDER BY` clause.

{{<warning>}}
You cannot use column aliases in the `WHERE` clause of `SELECT`.
{{</warning>}}

## Limitting rows

To return only upto a certain number of rows, you can use the LIMIT clause to set a ceiling on the no.of rows returned.

For example, to return just one row, you set to `LIMIT 1` like:

```sql
SELECT * FROM employees LIMIT 1;
```

You will get just one row, (usually the first row without the LIMIT) like:

```caddyfile{.nocopy}
 employee_no |     name     | department
-------------+--------------+------------
        1223 | Lucille Ball | Operations
```

## Skipping rows

To skip certain number of rows before returning the result you can use the `OFFSET` clause. For example, to skip the first row that would be returned by a `select *` you can run,

```sql
SELECT * FROM employees OFFSET 1;
```

You will get the remaining 3 rows other than the first one.

```caddyfile{.nocopy}
 employee_no |      name      | department
-------------+----------------+------------
        1224 | John Zimmerman | Sales
        1221 | John Smith     | Marketing
        1222 | Bette Davis    | Sales
```

{{<tip>}}
You can accomplish pagination by using `LIMIT` and `OFFSET` in conjunction. For eg, you can get the `N`th page of `M` results by adding `OFFSET (N-1)*M LIMIT M`.
{{</tip>}}

## Matching strings

There are cases when you do not know the exact query parameter but have an idea of a partial parameter. Using the `LIKE` operator allows you to match this partial information with existing data based on a pattern recognition.

For example, to find the details of all employees whose name starts with `Luci`, you can execute the following query:

```sql
SELECT * FROM employees WHERE name LIKE 'Luci%';
```

You will get the details of all those whose names start with `Luci`.

```caddyfile{.nocopy}
 employee_no |     name     | department
-------------+--------------+------------
        1223 | Lucille Ball | Operations
```

## Grouping

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

Another way to execute complex hierarchical queries is to use a `tablefunc` extension. This extension provides several table functions, such as, for example, `normal_rand()` that creates values picked using a pseudorandom generator from an ideal normal distribution. For more information and examples, see [tablefunc](../../../explore/ysql-language-features/pg-extensions/extension-tablefunc).
