---
title: Reading data
headerTitle: Read data
linkTitle: Read data
description: Queries and joins to read data in YSQL
headcontent: Interact with data using SELECT, filters, and joins
menu:
  v2.25:
    identifier: explore-ysql-language-features-queries-joins
    parent: explore-ysql-language-features
    weight: 300
type: docs
---

Querying is the process of retrieving specific information from a database. The following guide shows how to query data from a database, starting with basic retrieval techniques and expanding into more advanced concepts like filtering, sorting, joining tables, and aggregating results. Whether you're a beginner learning how to extract information or an advanced user optimizing queries for performance, this guide will help you efficiently interact with your data.

The SELECT statement is used to retrieve data from tables. It begins by specifying the columns to be fetched, followed by the tables from which to retrieve the data. Additionally, it may include optional conditions for filtering the results and specifying the order in which the data should be returned.

## Setup

{{% explore-setup-single-new %}}

Create the following table and add corresponding data.

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

## Basic querying

### All data

To retrieve all the data from the table, you can use the following query:

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

### Just one column

Suppose you want to fetch just the name of the all the employees. For this, you can run the following query:

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

### Multiple columns

To fetch both the name and department of the all the employees, you can run the following query:

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

### Column aliases

Suppose you want to fetch both the name and department together as a single value of the all the employees. Because there is no defined name for the combined value, by default the system generates a column name as `?column?`. You can use the AS clause to assign a name for this generated column as follows:

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
Column aliases may contain spaces. In such a case, you need enclose the alias in double quotes to produce multi-word headers.
{{</tip>}}

### Order data

You can use the ORDER BY clause to order/sort your result set on a specific condition. You can also specify the kind of ordering you need (for example, ASC or DESC). For example, to select columns and order the results by the name of the employee, you can run the following query:

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
The default sort order `ASC` is used when not specified explicitly.
{{</tip>}}

You can also specify different sort orders for different columns. For example, to fetch the data sorted by the department in ascending order, and then sort the rows with the same department by name in descending order, you can run the following query:

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

### Order NULLs

Sorting rows that contain NULL values is usually done using the ORDER BY clause with the NULLS FIRST and NULLS LAST options. These options allow you to control the placement of NULL values relative to non-null values: NULLS FIRST positions NULL before non-null values, while NULLS LAST positions NULL after non-null values.

The default behavior for sorting NULL values depends on whether the DESC or ASC option is used in the ORDER BY clause. When using DESC, the default is NULLS FIRST, and with ASC, the default is NULLS LAST.

For example, to order results by department in ascending order displaying rows with missing departments first, you can run the following query:

```sql
SELECT department FROM employees
  ORDER BY department ASC NULLS FIRST;
```

### Subqueries

A subquery is a query nested inside another SQL query. It is used to perform a query in the context of a larger query, allowing more complex data retrieval and manipulation. Subqueries can be placed inside SELECT, INSERT, UPDATE, or DELETE statements, or in clauses such as WHERE, FROM, or HAVING.

Suppose you want to find students with scores above the average score in the scores table. For this you can run the following query:

```sql
SELECT name
FROM students
WHERE id IN (
    SELECT id
    FROM scores
    WHERE score > (SELECT AVG(score) FROM scores)
);
```

`SELECT AVG(score) FROM scores` is the subquery that calculates the average score and passes it on to `SELECT id FROM scores`, which itself is another subquery that finds the IDs of the students with a score greater than the average score.

### CTEs

Common Table Expressions (CTEs) allow you to execute recursive, hierarchical, and other types of complex queries in a simplified manner by breaking down these queries into smaller units. A CTE exists only during the query execution and represents a temporary result set that you can reference like a table from another SQL statement.

You can use the following syntax to create a basic CTE:

```sql
WITH cte_name (columns) AS (cte_query) statement;
```

For example, to create a table of combinations of all subjects and student names, you can using the following syntax:

```sql
WITH subjects AS ( --> cte name : subjects
    SELECT DISTINCT(subject) FROM scores AS subject --> cte query
)
-- below is the statement
SELECT name, subject
  FROM subjects CROSS JOIN students
  ORDER BY name, subject;
```

In this case, the distinct list of subjects is formed via the CTE query and referred as `subjects` table in the SQL statement.

## Filtering

Use the WHERE clause to filter the results based on a condition. Only the rows that satisfy a specified condition are included in the result set.

For example, to fetch rows only from the `Marketing` department, you can add a condition `department = 'Marketing'` in the WHERE clause as follows:

```sql
SELECT * FROM employees WHERE department = 'Marketing';
```

Following is the output produced by the preceding example:

```caddyfile{.nocopy}
 employee_no |    name    | department
-------------+------------+------------
        1221 | John Smith | Marketing
```

You can use any of the supported [operators and expressions](../expressions-operators/) (except ALL, ANY, and SOME) to combine multiple conditions to fetch the results you need.

For example, to fetch all employees with names starting with either B or L, you can use the OR operator.

```sql
SELECT * FROM employees WHERE name LIKE 'B%' OR name LIKE 'L%';
```

Following is the output produced by the preceding example:

```caddyfile{.nocopy}
 employee_no |     name     | department
-------------+--------------+------------
        1223 | Lucille Ball | Operations
        1222 | Bette Davis  | Sales
```

During the query execution, the WHERE clause is evaluated after the FROM clause but before the SELECT and ORDER BY clauses.

{{<warning>}}
You cannot use column aliases in the WHERE clause of SELECT.
{{</warning>}}

### Limit rows

To return up to a specified number of rows only, you can use the LIMIT clause to set a ceiling on the number of rows returned.

For example, to return just one row, you set to `LIMIT 1` as follows:

```sql
SELECT * FROM employees LIMIT 1;
```

You will get just one row, (usually the first row without the LIMIT), as follows:

```caddyfile{.nocopy}
 employee_no |     name     | department
-------------+--------------+------------
        1223 | Lucille Ball | Operations
```

### Skip rows

To skip a certain number of rows before returning the result, use the OFFSET clause. For example, to skip the first row that would be returned by a `select *`, you can run the following:

```sql
SELECT * FROM employees OFFSET 1;
```

You will get the remaining 3 rows.

```caddyfile{.nocopy}
 employee_no |      name      | department
-------------+----------------+------------
        1224 | John Zimmerman | Sales
        1221 | John Smith     | Marketing
        1222 | Bette Davis    | Sales
```

{{<tip>}}
You can accomplish pagination by using LIMIT and OFFSET in conjunction. For example, you can get the Nth page of M results by adding `OFFSET (N-1)*M LIMIT M`.
{{</tip>}}

### Match strings

For cases where you don't know the exact query parameter but have an idea of a partial parameter, use LIKE. Using the LIKE operator allows you to match this partial information with existing data based on a pattern recognition.

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

### Duplicate rows

You can use the DISTINCT clause to remove duplicate rows from a query result. The DISTINCT clause keeps one row for each set of duplicates. You can apply this clause to columns included in the SELECT statement's select list. For example, to get the list of departments, you can run:

```sql
SELECT DISTINCT department FROM employees;
```

You will get the following output. Note that even though there are 2 employees in Sales, only one row for Sales has been returned. This is the effect of DISTINCT.

```caddyfile{.nocopy}
 department
------------
 Marketing
 Operations
 Sales
```

## Grouping

Use GROUP BY when you need to organize or summarize data based on specified columns. Grouping is often combined with aggregate functions like COUNT(), SUM(), AVG(), or MAX() to perform calculations on each group. For example, to calculate the number of employees in each department, you can do the following:

```sql
SELECT department, COUNT (employee_no)
  FROM employees
  GROUP BY department;
```

You will get the following output.

```caddyfile{.nocopy}
 department | count
------------+-------
 Marketing  |     1
 Operations |     1
 Sales      |     2
```

To restrict the grouped data, use the HAVING clause. While the WHERE clause filters rows before grouping, HAVING filters groups after the grouping is performed. For example, to find departments which have 2 or more employees, you need to run:

```sql
SELECT department, COUNT (employee_no)
  FROM employees
  GROUP BY department
  HAVING COUNT (employee_no) >= 2;
```

You will get the following output.

```caddyfile{.nocopy}
 department | count
------------+-------
 Sales      |     2
```

The HAVING clause extends the power of GROUP BY by allowing you to apply conditions to grouped data, making it invaluable for complex data analysis and reporting tasks.

## Join columns

Joins are used to combine rows from two or more tables based on a related column. The related column is usually a foreign key that establishes a relationship between the tables. A join condition specifies how the rows from one table should be matched with the rows from another table. It can be defined in one of ON, USING, or WHERE clauses.

YugabyteDB supports multiple types of joins. For the purpose of illustration, consider the following schema of students and their respective scores in different subjects.

```sql
CREATE TABLE students (
    id int,
    name varchar(255),
    PRIMARY KEY(id)
);

CREATE TABLE scores (
    id int,
    subject varchar(100),
    score int,
    PRIMARY KEY(id, subject)
);

-- Not needed, but helpful for speeding up queries
CREATE INDEX idx_name on students(name);
CREATE INDEX idx_id on scores(id);
```

Load some data into the tables by adding some students:

```sql
INSERT INTO students (id,name)
    VALUES (1, 'Natasha'), (2, 'Lisa'), (3, 'Mike'), (4, 'Michael'), (5, 'Anthony');
```

Add some scores to the students as follows:

```sql
WITH subjects AS (
    SELECT unnest(ARRAY['English', 'History', 'Math', 'Spanish', 'Science']) AS subject
)
INSERT INTO scores (id, subject, score)
    SELECT id, subject, (40+random()*60)::int AS score
        FROM subjects CROSS JOIN students
        WHERE id <= 3
        ORDER BY id;

INSERT INTO scores (id, subject, score) VALUES (10, 'English', 40);
```

### Inner join

An inner join combines rows from two or more tables based on a related column. Only the rows where there is a match in both tables are included in the result set. Inner joins are helpful when you need data from multiple tables that are related to each other through foreign keys or other shared columns. For example, to retrieve the scores of the students in their respective subjects, you can run the following:

```sql
SELECT students.name, scores.subject, scores.score
FROM students
INNER JOIN scores ON students.id = scores.id;
```

You will get an output similar to:

```caddyfile{.nocopy}
  name   | subject | score
---------+---------+-------
 Natasha | English |    85
 Natasha | History |    94
 Natasha | Math    |    97
 ...
 Lisa    | Science |    41
 Lisa    | Spanish |    94
 Mike    | English |    55
 Mike    | History |    47
 Mike    | Math    |    66
 Mike    | Science |    50
 Mike    | Spanish |    98
...
```

In this instance, if a student exists in the students table but has no recorded scores in the scores table, that student will not appear in the result.

### Left outer join

The LEFT OUTER JOIN (or LEFT JOIN) retrieves all records from the "left" table (the first table listed in the query) and includes matching records from the "right" table (the second table). If there is no match in the right table, the result still includes all rows from the left table, but with NULL values for the columns from the right table. This is helpful for identifying missing relationships or data gaps between tables.

For example, when you fetch the `Math` scores of all the students, you can identify that two students did not take the course.

```sql
SELECT students.name, scores.subject, scores.score
  FROM students
  LEFT OUTER JOIN scores ON
  students.id = scores.id and subject = 'Math';
```

The following output includes all five student names, but has `null` value for subject and score for the students who did not have corresponding records in the scores table.

```caddyfile{.nocopy}
  name   | subject | score
---------+---------+-------
 Natasha | Math    |    97
 Lisa    | Math    |    85
 Mike    | Math    |    66
 Anthony | null    |  null
 Michael | null    |  null
```

### Right outer join

The RIGHT OUTER JOIN (or RIGHT JOIN) retrieves all records from the "right" table (the second table listed in the query) and includes matching records from the "left" table (the first table). If there is no match in the left table, the result still includes all rows from the right table, but with NULL values for the columns from the left table.

If you retrieve the scores of the students in English, you will find that there is one unknown student who took the course.

```sql
SELECT students.name, scores.subject
FROM students
RIGHT OUTER JOIN scores ON students.id = scores.id WHERE subject='English';
```

The result set has one row with a `null` name, which means that there is a score in the scores table for a student who does not exist in the students table.

```caddyfile{.nocopy}
  name   | subject
---------+---------
 Natasha | English
 Lisa    | English
 Mike    | English
 null    | English
```

### Full outer join

The full outer join combines the LEFT and RIGHT outer joins. It returns all records from both the left and right tables, including matching rows. Where there is no match between the two tables, the result includes rows from both tables, but with NULL values in the columns from the table that does not have a corresponding match.

For example, when you do a full outer join on the students and scores table as follows:

```sql
SELECT students.name, scores.subject
FROM students
FULL OUTER JOIN scores ON students.id = scores.id;
```

You will see `null` values from both tables.

```caddyfile{.nocopy}
  name   | subject
---------+---------
 Natasha | English
 Natasha | History
...
 null    | English
 Lisa    | English
 Lisa    | History
...
 Mike    | Science
 Mike    | Spanish
 Anthony | null
 Michael | null
(18 rows)
```

### Cross join

A CROSS JOIN returns the Cartesian product of two tables, meaning it combines every row from the first table with every row from the second table. This type of join doesn't require any condition to match rows between the tables. For example, when you cross join the students table (`5` rows) with the scores (`16` rows), the result set includes all combinations of student names with all subjects (`5*16 = 80` rows)

```sql
SELECT name, subject
FROM students
CROSS JOIN scores;
```

```caddyfile{.nocopy}
  name   | subject
---------+---------
 Anthony | English
 Anthony | History
 Anthony | Math
 Anthony | Science
 Anthony | Spanish
 Anthony | English
...
 Mike    | Math
 Mike    | Science
 Mike    | Spanish
 Mike    | English
 Mike    | History
 Mike    | Math
 Mike    | Science
 Mike    | Spanish
(80 rows)
```
