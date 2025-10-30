---
title: Expressions and operators
linkTitle: Expressions and operators
description: Expressions and operators in YSQL
menu:
  v2.25:
    identifier: explore-ysql-language-features-expressions-operators
    parent: explore-ysql-language-features
    weight: 500
type: docs
---

YugabyteDB provides a rich set of expressions and operators that form the building blocks of SQL queries and data manipulation. The following is a comprehensive guide to the various types of expressions and operators available. Understanding these expressions and operators is essential for writing efficient and powerful SQL queries.

## Setup

{{% explore-setup-single-new %}}

## Mathematical operators

In addition to the standard operators `+, -, *, /` for addition, subtraction, multiplication, and division, YugabyteDB also supports the following operators.

| Operator | Description                                | Example    | Result |
| -------- | ------------------------------------------ | ---------- | ------ |
| %        | Remainder                                  | 5 % 4      | 1      |
| ^        | Exponent (association of left to right)    | 2.0 ^ 3.0  | 8      |
| \|/      | Square root                                | \|/ 16.0   | 4      |
| \|\|/    | Cube root                                  | \|\|/ 27.0 | 3      |
| !        | Factorial (suffix)                         | 5 !        | 120    |

The following examples show how to use mathematical operators in a SELECT statement:

```sql
SELECT 1+2;
```

```sql
SELECT 6/2;
```

```sql
SELECT ||/ 27.0;
```

## Comparison operators

Comparison operators are binary. They return a boolean value of true or false, depending on whether or not the comparison was asserted.

In addition to the standard numerical comparison operators `<, >, <=, >=, =`, YugabyteDB also supports the following operators.

| Operator | Description              | Example |
| -------- | ------------------------ | ------- |
| <>       | Not equal                | a <> 5  |
| !=       | Not equal                | a != 5  |

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  salary integer
);

INSERT INTO employees (employee_no, name, department, salary)
  VALUES
  (1221, 'John Smith', 'Marketing', 50000),
  (1222, 'Bette Davis', 'Sales', 55000),
  (1223, 'Lucille Ball', 'Operations', 70000),
  (1224, 'John Zimmerman', 'Sales', 60000);
```

The following example shows a SELECT statement that returns employees whose employee numbers are greater than 1222:

```sql
SELECT * FROM employees WHERE employee_no > 1222;
```

You should see the following output:

```caddyfile{.nocopy}
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
1224        | John Zimmerman   | Sales        | 60000
```

## String operators

The following table describes a string operator that you can use in YSQL.

| Operator | Description                                         | Example                        | Result |
| -------- | --------------------------------------------------- | ------------------------------ | ------ |
| \|\|     | Concatenates two strings or a string and non-string | 'wo' \|\| 'rd' <br>'number' \|\| 55 <br>2021 \|\| 'is here' | word <br> number 55 <br> 2021 is here

## Logical operators

The following table lists logical operators that you can use in YSQL.

| Operator | Description                                                    |
| -------- | -------------------------------------------------------------- |
| AND      | Allows the existence of multiple conditions in a WHERE clause. |
| NOT      | Negates the meaning of another operator. For example, NOT IN, NOT BETWEEN. |
| OR       | Combines multiple conditions in a WHERE clause.                |

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a SELECT statement that returns employees whose employee numbers are greater than or equal to 1222 and salary is greater than or equal to 70000:

```sql
SELECT * FROM employees WHERE employee_no >= 1222 AND SALARY >= 70000;
```

You should see the following output:

```caddyfile{.nocopy}
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
```

## Bitwise operators

The following table lists bitwise operators that you can use in YSQL.

| Operator | Description                                                  | Example    | Result |
| -------- | ------------------------------------------------------------ | ---------- | ------ |
| &        | Bitwise AND <br>Copies a bit to the result if it exists in both operands. | 91 & 15 | 11 |
| \|       | Bitwise OR<br/>Copies a bit to the result if it exists in either operand. | 32 \| 3 | 35 |
| #        | Bitwise XOR                                                  | 17 # 5     | 20  |
| ~        | Bitwise NOT <br>Flips bits.                                  | ~1         | -2  |
| <<       | Bitwise shift left <br>Moves the value of the left operand left by the number of bits specified by the right operand.   | 1 << 4 | 16  |
| >>       | Bitwise shift right<br/>Moves the value of the left operand right by the number of bits specified by the right operand. | 8 >> 2 | 2   |

Bitwise operators can be applied to bit data types and data types related to it.

## CAST operator

You can use the CAST operator to convert a value of one data type to another.

The following is the syntax of the CAST operator:

```sql
CAST (expression AS new_type);
```

*expression* is a constant, a table column, or an expression that evaluates to a value. *new_type* is a data type to which to convert the result of the *expression*.

The following example converts a string constant to an integer:

```sql
SELECT CAST ('25' AS INTEGER);
```

The following example converts a string constant to a date:

```sql
SELECT
  CAST ('2021-02-02' AS DATE),
  CAST ('02-DEC-2020' AS DATE);
```

You should see the following output:

```caddyfile{.nocopy}
 date       | date
------------+----------------
 2021-02-02 | 2020-12-02
```

YSQL supports another syntax for casting data types:

```sql
expression::type
```

The following example demonstrates the use of the :: operator:

```sql
SELECT
'25'::INTEGER,
'02-DEC-2020'::DATE;
```

## Boolean expressions

Boolean expressions retrieve data by matching a single value. The expression is included in the WHERE clause.

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a SELECT statement that returns employees whose salary is 60000:

```sql
SELECT * FROM employees WHERE salary = 60000;
```

You should see the following output:

```caddyfile{.nocopy}
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1224        | John Zimmerman   | Sales        | 60000
```

## Numeric expressions

The purpose of numeric expressions is to perform a mathematical operation on a query.

The following example shows how to use a basic numerical expression in a SELECT statement:

```sql
SELECT (10 + 5) AS ADDITION;
```

You should see the following output:

```caddyfile{.nocopy}
addition
--------------
15
```

You can also use predefined functions such as `avg()`, `sum()`, or `count()` to perform aggregate data calculations on a table or a column.

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a SELECT statement that returns the number of employee rows:

```sql
SELECT count(*) AS "rows" FROM employees;
```

You should see the following output:

```caddyfile{.nocopy}
rows
-----------
4
```

## Date expressions

Date expressions retrieve the current system date and time, as shown in the following SELECT statement example:

```sql
SELECT CURRENT_TIMESTAMP;
```

You should see the following output:

```caddyfile{.nocopy}
now
--------------
2021-03-15 14:38:28.078+05:30
```

You can use these expressions during data manipulation.

## CASE expression

The CASE expression enables you to add if-else logic to your queries (for example, to SELECT, WHERE, GROUP BY, and HAVING).

The following is the syntax of the general form of the CASE expression:

```sql{.nocopy}
CASE
  WHEN condition_a  THEN result_a
  WHEN condition_b  THEN result_b
  [WHEN ...]
  [ELSE result_else]
END
```

Each condition is a boolean expression. If it evaluates to false, CASE continues evaluation until it finds a condition that evaluates to true. If a condition evaluates to true, CASE returns the result that follows the condition and stops evaluation. If all conditions evaluate to false, CASE returns the result that follows ELSE. If ELSE is not included in the statement, the CASE expression returns NULL.

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
  (1224, 'John Zimmerman', 'Sales'),
  (1225, 'Lee Bo', 'Sales'),
  (1226, 'Frank Sinatra', 'Operations');
```

The following example uses the CASE expression in the SELECT statement to create labels for employees based on their employee number: if the number is smaller than 1223, the employee is a senior; if the number is greater than 1223 and smaller than or equal to 1225, the employee is intermediate; if the number is greater than 1225, the employee is a junior:

```sql
SELECT employee_no, name,
  CASE
    WHEN employee_no > 0
      AND employee_no <= 1223 THEN 'Senior'
    WHEN employee_no > 1223
      AND employee_no <= 1225 THEN 'Intermediate'
    WHEN employee_no > 1225 THEN 'Junior'
  END seniority
FROM employees
ORDER BY name;
```

The example produces the following output, with a column alias `seniority` placed after the CASE expression:

```caddyfile{.nocopy}
 employee_no  | name           | seniority
--------------+----------------+--------------
1222          | Bette Davis    | Senior
1226          | Frank Sinatra  | Junior
1221          | John Smith     | Senior
1224          | John Zimmerman | Intermediate
1225          | Lee Bo         | Intermediate
1223          | Lucille Ball   | Senior
```
