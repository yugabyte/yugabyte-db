---
title: Expressions and operators
linkTitle: Expressions and operators
description: Expressions and operators in YSQL
menu:
  stable:
    identifier: explore-ysql-language-features-expressions-operators
    parent: explore-ysql-language-features
    weight: 215
type: docs
---

This document describes how to use boolean, numeric, and date expressions, as well as basic operators. In addition, it provides information on conditional expression and operators.

{{% explore-setup-single %}}

## Basic operators

A large number of YSQL types have corresponding mathematical operators that are typically used for performing comparisons and mathematical operations. Operators allow you to specify conditions in YSQL statements and create links between conditions.

### Mathematical operators

The following table lists some of the mathematical operators that you can use in YSQL.

| Operator | Description                                | Example                 |
| -------- | ------------------------------------------ | ----------------------- |
| +        | Addition                                   | 1 + 2 results in 3      |
| -        | Subtraction                                | 1 - 2 results in -1     |
| *        | Multiplication                             | 2 * 2 results in 4      |
| /        | Division                                   | 6 / 2 results in 3      |
| %        | Remainder                                  | 5 % 4 results in 1      |
| ^        | Exponent (association of left to right)    | 2.0 ^ 3.0 results in 8  |
| \|/      | Square root                                | \|/ 16.0 results in 4   |
| \|\|/    | Cube root                                  | \|\|/ 27.0 results in 3 |
| !        | Factorial (suffix)                         | 5 ! results in 120      |

The following examples show how to use mathematical operators in a `SELECT` statement:

```sql
SELECT 1+2;
```

```sql
SELECT 6/2;
```

```sql
SELECT ||/ 27.0;
```

### Comparison operators

Comparison operators are binary. They return a `boolean` value `true` or `false`, depending on whether or not the comparison was asserted.

The following table lists comparison operators that you can use in YSQL.

| Operator | Description              | Example |
| -------- | ------------------------ | ------- |
| <        | Less than                | a < 5   |
| >        | Greater than             | a > 5   |
| <=       | Less than or equal to    | a <= 5  |
| >=       | Greater than or equal to | a >= 5  |
| =        | Equal                    | a = 5   |
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

The following example shows a `SELECT` statement that returns employees whose employee numbers are greater than 1222:

```sql
SELECT * FROM employees WHERE employee_no > 1222;
```

The following is the output produced by the preceding example:

```output
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
1224        | John Zimmerman   | Sales        | 60000
```

### String operators

The following table describes a string operator that you can use in YSQL.

| Operator | Description                                         | Example                        |
| -------- | --------------------------------------------------- | ------------------------------ |
| \|\|     | Concatenates two strings or a string and non-string <br> | 'wo' \|\| 'rd' results in word <br>'number' \|\| 55 results in number 55 <br>2021 \|\| 'is here' results in 2021 is here |

### Logical operators

The following table lists logical operators that you can use in YSQL.

| Operator | Description                                                  |
| -------- | ------------------------------------------------------------ |
| AND      | Allows the existence of multiple conditions in a `WHERE` clause. |
| NOT      | Negates the meaning of another operator. For example, `NOT IN`, `NOT BETWEEN`. |
| OR       | Combines multiple conditions in a `WHERE` clause.            |

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a `SELECT` statement that returns employees whose employee numbers are greater than or equal to 1222 and salary is greater than or equal to 70000:

```sql
SELECT * FROM employees WHERE employee_no >= 1222 AND SALARY >= 70000;
```

The following is the output produced by the preceding example:

```output
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
```

### Bitwise operators

The following table lists bitwise operators that you can use in YSQL.

| Operator | Description                                                  | Example               |
| -------- | ------------------------------------------------------------ | --------------------- |
| &        | Bitwise AND <br>Copies a bit to the result if it exists in both operands. | 91 & 15 results in 11 |
| \|       | Bitwise OR<br/>Copies a bit to the result if it exists in either operand. | 32 \| 3 results in 35 |
| #        | Bitwise XOR                                                  | 17 # 5 results in 20  |
| ~        | Bitwise NOT <br>Flips bits.                                  | ~1 results in -2      |
| <<       | Bitwise shift left <br>Moves the value of the left operand left by the number of bits specified by the right operand. | 1 << 4 results in 16  |
| >>       | Bitwise shift right<br/>Moves the value of the left operand right by the number of bits specified by the right operand. | 8 >> 2 results in 2   |

Bitwise operators can be applied to bit data types and data types related to it.

## Basic expressions

An expression combines values, operators, and YSQL functions that evaluate to a value.

Typical YSQL expressions are similar to formulas. The following types of expressions are supported:

### Boolean expressions

Boolean expressions retrieve data by matching a single value. The expression is included in the `WHERE` clause.

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a `SELECT` statement that returns employees whose salary is 60000:

```sql
SELECT * FROM employees WHERE salary = 60000;
```

The following is the output produced by the preceding example:

```output
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1224        | John Zimmerman   | Sales        | 60000
```

### Numeric expressions

The purpose of numeric expressions is to perform a mathematical operation on a query.

The following example shows how to use a basic numerical expression in a `SELECT` statement:

```sql
SELECT (10 + 5) AS ADDITION;
```

The following is the output produced by the preceding example:

```output
addition
--------------
15
```

You can also use predefined functions such as `avg()`, `sum()`, or `count()` to perform aggregate data calculations on a table or a column.

The following example uses the sample table from [Comparison operators](#comparison-operators) and shows a `SELECT` statement that returns the number of employee rows:

```sql
SELECT count(*) AS "rows" FROM employees;
```

The following is the output produced by the preceding example:

```output
rows
-----------
4
```

### Date expressions

Date expressions retrieve the current system date and time, as shown in the following  `SELECT` statement example:

```sql
SELECT CURRENT_TIMESTAMP;
```

The following is the output produced by the preceding example:

```output
now
--------------
2021-03-15 14:38:28.078+05:30
```

 You can use these expressions during data manipulation.

## Conditional expressions and operators

Conditional expressions and operators assist you with forming conditional queries. YSQL supports `CASE` and `CAST`, among others.

### CASE

The `CASE` expression enables you to add if-else logic to your queries (for example, to `SELECT`, `WHERE`, `GROUP BY`, and `HAVING` ).

The following is the syntax of the general form of the `CASE` expression:

```output
CASE
  WHEN condition_a  THEN result_a
  WHEN condition_b  THEN result_b
  [WHEN ...]
  [ELSE result_else]
END
```

Each condition is a boolean expression. If it evaluates to `false`, `CASE` continues evaluation until it finds a condition that evaluates to `true`. If a condition evaluates to `true`, `CASE` returns the result that follows the condition and stops evaluation. If all conditions evaluate to `false`, `CASE` returns the result that follows `ELSE`. If `ELSE` is not included in the statement, the `CASE` expression returns `NULL`.

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

The following example uses the `CASE` expression in the `SELECT` statement to create labels for employees based on their employee number: if the number is smaller than 1223, the employee is a senior; if the number is greater than 1223 and smaller than or equal to 1225, the employee is intermediate; if the number is greater than 1225, the employee is a junior:

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

The preceding example produces the following output, with a column alias `seniority` placed after the `CASE` expression:

```output
 employee_no  | name           | seniority
--------------+----------------+--------------
1222          | Bette Davis    | Senior
1226          | Frank Sinatra  | Junior
1221          | John Smith     | Senior
1224          | John Zimmerman | Intermediate
1225          | Lee Bo         | Intermediate
1223          | Lucille Ball   | Senior
```

### CAST

You can use the `CAST` operator to convert a value of one data type to another.

The following is the syntax of the `CAST` operator:

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

The preceding example produces the following output:

```output
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
