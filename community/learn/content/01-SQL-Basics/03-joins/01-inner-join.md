---
title: "INNER JOIN"
---

## SELECT with an INNER JOIN

In this exercise we will query the _employees_ table using an _INNER JOIN_ with the _orders_ table.

```
SELECT DISTINCT employees.employee_id,
       employees.last_name,
       employees.first_name,
       employees.title
FROM employees
INNER JOIN orders ON employees.employee_id = orders.employee_id;
```
_The query should return 9 rows._

## SELECT with an INNER JOIN and ORDER BY

In this exercise we will query the _employees_ table by using an _INNER JOIN_ with the _orders_ table and order the results by _product\_id_ in a descending order.

```
SELECT DISTINCT products.product_id,
                products.product_name,
                order_details.unit_price
FROM products
INNER JOIN order_details ON products.product_id = order_details.product_id
ORDER BY products.product_id DESC;
```
_The query should return 156 rows._

## SELECT with an INNER JOIN and WHERE clause

In this exercise we will query the _products_ table by using an _INNER JOIN_ with the _order\_details_ table where the _product\_id_ equals 2.

```
SELECT DISTINCT products.product_id,
                products.product_name,
                order_details.order_id
FROM products
INNER JOIN order_details ON products.product_id = order_details.product_id
WHERE products.product_id = 2;
```
_The query should return 44 rows._

## SELECT with an INNER JOIN and three tables

In this exercise we will query the _orders_ table by using an _INNER JOIN_ with the _customers_ and _employees_table.

```
SELECT customers.company_name,
       employees.first_name,
       orders.order_id
FROM orders
INNER JOIN customers ON customers.customer_id = orders.customer_id
INNER JOIN employees ON employees.employee_id = orders.employee_id;
```
_The query should return 830 rows._