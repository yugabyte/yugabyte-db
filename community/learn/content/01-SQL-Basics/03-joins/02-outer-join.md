---
title: "OUTER JOIN"
---

## SELECT with a LEFT OUTER JOIN

In this exercise we will query the _customers_ table using a _LEFT JOIN_ with the _orders_ table.

```
SELECT DISTINCT customers.company_name,
                customers.contact_name,
                orders.ship_region
FROM customers
LEFT JOIN orders ON customers.region = orders.ship_region
ORDER BY company_name DESC;
```
_The query should return 91 rows._

## SELECT with RIGHT OUTER JOIN

In this exercise we will query _customers_ table using a _FULL OUTER JOIN_ on _orders_.

```
SELECT DISTINCT customers.company_name,
                customers.contact_name,
                orders.ship_region
FROM customers
RIGHT OUTER JOIN orders ON customers.region = orders.ship_region
ORDER BY company_name DESC;
```
_The query should return 33 rows._

## SELECT with FULL OUTER JOIN

In this exercise we will query the _customers_ table using a FULL OUTER JOIN with the _orders_ table.

```
SELECT DISTINCT customers.company_name,
                customers.contact_name,
                orders.ship_region
FROM customers
FULL OUTER JOIN orders ON customers.region = orders.ship_region
ORDER BY company_name DESC;
```
_The query should return 93 rows._

## SELECT with FULL OUTER JOIN with only unique rows in both tables

In this exercise we will query the _employees_ table using a _FULL OUTER JOIN_ on the _orders_ table using only the unique rows in each table.

```
SELECT DISTINCT employees.employee_id,
                employees.last_name,
                orders.customer_id
FROM employees
FULL OUTER JOIN orders ON employees.employee_id = orders.employee_id;
```
_The query should return 464 rows._