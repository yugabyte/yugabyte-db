---
title: "SELECT"
---

**SELECT data from one column**

In this exercise we will select all the data in the _company\_names_ column from the _customers_ table.

```
select company_name from customers;
```
_The query should return 91 rows._

**SELECT data from multiple columns**

In this exercise we will select all the data in three columns from the _employees_ table.

```
select employee_id,first_name,last_name,birth_date from employees;
```
_The query should return 9 rows._

**SELECT data from all the columns and rows in a table**

In this exercise we will select all the data, from all columns in the _order_details_ table.

```
select * from order_details;
```
_The query should return 2155 rows._

**SELECT with an expression**

In this exercise we will combine the _first\_name_ and _last\_name columns_ to give us full names, along with their titles from the _employees_ table.

```
select first_name || '' || last_name as full_name,title from employees;
```
_The query should return 9 rows._

**SELECT with an expression, but without a FROM clause**

In this exercise we will use an expression, but omit specifying a table because it doesn't require one.

```
SELECT 5 * 3 AS result;
```
_The query should return 1 row with a result of 15._

**SELECT with a column alias**

In this exercise we will use the alias _title_ for the _contact_title_ column and output all the rows.

```
SELECT contact_title AS title FROM customers;
```

_The query should return 91 rows._

**SELECT with a table alias**

In this exercise we will use the alias _details_ for the _order_\__details_ table and output all the rows.

```
SELECT product_id, discount FROM order_details AS details;
```

_The query should return 2155 rows._

