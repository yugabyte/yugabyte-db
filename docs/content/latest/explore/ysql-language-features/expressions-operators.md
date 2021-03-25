---
title: Expressions and Operators
linkTitle: Expressions and Operators
description: Expressions and Operators in YSQL
headcontent: Expressions and Operators in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-expressions-operators
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

This document describes how to use boolean, numeric, and date expressions, as well as basic operators. In addition, it provides information on conditional expression and operators.

## Basic Operators

A large number of YSQL types have corresponding mathematical operators that are typically used for performing comparisons and mathematical operations. Operators allow you to specify conditions in YSQL statements and create links between conditions.

### Mathematical Operators

The following table lists some of the mathematical operators that you can use in YSQL.

| Operator | Description                                | Example                 |
| -------- | ------------------------------------------ | ----------------------- |
| +        | Addition                                   | 1 + 2 results in 3      |
| -        | Substractioin                              | 1 - 2 results in -1     |
| *        | Multiplication                             | 2 * 2 results in 4      |
| /        | Division                                   | 6 / 2 results in 3      |
| %        | Remainder                                  | 5 % 4 results in 1      |
| ^        | Exponent (associatiation of left to right) | 2.0 ^ 3.0 results in 8  |
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

### Comparison Operators

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

```
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
1224        | John Zimmerman   | Sales        | 60000
```

### String Operators

The following table describes a string operator that you can use in YSQL.

| Operator | Description                                         | Example                        |
| -------- | --------------------------------------------------- | ------------------------------ |
| \|\|     | Concatenates two strings or a string and non-string <br> | 'wo' \|\| 'rd' results in word <br>'number' \|\| 55 results in number 55 <br>2021 \|\| 'is here' results in 2021 is here |

### Logical Operators

The following table lists logical operators that you can use in YSQL.

| Operator | Description                                                  |
| -------- | ------------------------------------------------------------ |
| AND      | Allows the existence of multiple conditions in a `WHERE` clause. |
| NOT      | Negates the meaning of another operator. For example, `NOT IN`, `NOT BETWEEN`. |
| OR       | Combines multiple conditions in a `WHERE` clause.            |

The following example uses the sample table from [Comparison Operators](#comparison-operators) and shows a `SELECT` statement that returns employees whose employee numbers are greater than or equal to 1222 and salary is greater than or equal to 70000:

```sql
SELECT * FROM employees WHERE employee_no >= 1222 AND SALARY >= 70000;
```

The following is the output produced by the preceding example:

```
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1223        | Lucille Ball     | Operations   | 70000
```

### Bitwise Operators

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

## Basic Expressions

An expression combines values, operators, and YSQL functions that evaluate to a value.

Typical YSQL expressions are similar to formulas. The following types of expressions are supported:

### Boolean Expressions

Boolean expressions retrieve data by matching a single value. The expression is included in the `WHERE` clause.

The following example uses the sample table from [Comparison Operators](#comparison-operators) and shows a `SELECT` statement that returns employees whose salary is 60000:

```sql
SELECT * FROM employees WHERE salary = 60000;
```

The following is the output produced by the preceding example:

```
employee_no | name             | department   | salary
------------+------------------+--------------+--------------
1224        | John Zimmerman   | Sales        | 60000
```

### Numeric Expressions

The purpose of numeric expressions is to perform a mathematical operation on a query.

The following example shows how to use a simple numerical expression in a `SELECT` statement:

```sql
SELECT (10 + 5) AS ADDITION;
```

The following is the output produced by the preceding example:

```
addition
--------------
15
```

You can also use predefined functions such as `avg()`, `sum()`, or `count()` to perform aggregate data calculations on a table or a column.

The following example uses the sample table from [Comparison Operators](#comparison-operators) and shows a `SELECT` statement that returns the number of employee rows:

```sql
SELECT count(*) AS "rows" FROM employees;
```

The following is the output produced by the preceding example:

```
rows
-----------
4
```

### Date Expressions

Date expressions retrieve the current system date and time, as shown in the following  `SELECT` statement example:

```sql
SELECT CURRENT_TIMESTAMP;
```

The following is the output produced by the preceding example:

```
now
--------------
2021-03-15 14:38:28.078+05:30
```

 You can use these expressions during data manipulation.

## Conditional Expressions and Operators

Conditional expressions and operators assist you with forming conditional queries.

### CASE

The `CASE` expression enables you to add if-else logic to your queries (for example, in `SELECT`, `WHERE`, `GROUP BY`, and `HAVING` ). `CASE` has a general and a simple form.

The following illustrates the general form of the `CASE` statement:

```
     CASE 
      WHEN condition_1  THEN result_1
      WHEN condition_2  THEN result_2
      [WHEN ...]
      [ELSE else_result]
ENDCode language: SQL (Structured Query Language) (sql)
```

In this syntax, each condition (`condition_1`, `condition_2`…) is a boolean expression that returns either `true` or `false`.

When a condition evaluates to `false`, the `CASE` expression evaluates the next condition from the top to bottom until it finds a condition that evaluates to `true`.

If a condition evaluates to `true`, the `CASE` expression returns the corresponding result that follows the condition. For example, if the `condition_2` evaluates to `true`, the `CASE` expression returns the `result_2`. Also, it immediately stops evaluating the next expression.

In case all conditions evaluate to `false`, the `CASE` expression returns the result (`else_result`) that follows the `ELSE` keyword. If you omit the `ELSE` clause, the `CASE` expression returns `NULL`.