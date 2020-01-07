---
title: "NATURAL JOINs"
---

## SELECT with a NATURAL INNER JOIN

In this exercise we will query the _products_ table using a _NATURAL INNER JOIN_ with the _order\_details_ table.

```
SELECT DISTINCT products.product_id,
                products.product_name,
                order_details.unit_price
FROM products
NATURAL INNER JOIN order_details
ORDER BY products.product_id;
```
_The query should return 76 rows._

## SELECT with a NATURAL LEFT JOIN

In this exercise we will query the _customers_ table using a _NATURAL LEFT JOIN_ with the _orders_ table.

```
SELECT DISTINCT customers.customer_id,
                customers.contact_name,
                orders.ship_region
FROM customers NATURAL
LEFT JOIN orders
ORDER BY customers.customer_id DESC;
```
_The query should return 91 rows._

## SELECT with a NATURAL RIGHT JOIN

In this exercise we will query the _customers_ table using a _NATURAL RIGHT JOIN_ with the _orders_ table.

```
SELECT DISTINCT customers.company_name,
                customers.contact_name,
                orders.ship_region
FROM customers NATURAL
RIGHT JOIN orders
ORDER BY company_name DESC;
```
_The query should return 89 rows._

