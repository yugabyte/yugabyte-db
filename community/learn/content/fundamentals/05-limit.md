---
title: "LIMIT"
---

## SELECT with a LIMIT clause

In this exercise we'll query the _products_ table and return just the first 12 rows.

```
SELECT product_id,
       product_name,
       unit_price
FROM products
LIMIT 12;
```

_This query should return 12 rows._

## SELECT with LIMIT and OFFSET clauses

In this exercise we'll query the _products_ table and skip the first 4 rows before selecting the next 12.

```
SELECT product_id,
       product_name,
       unit_price
FROM products
LIMIT 12
OFFSET 4;
```

_This query should return 12 rows._

## SELECT with LIMIT and ORDER BY clauses

In this exercise we'll query the _products_ table, order the results in a descending order by _product\_id_ and limit the rows returned to 12.

```
SELECT product_id,
       product_name,
       unit_price
FROM products
ORDER BY product_id DESC
LIMIT 12;
```

_This query should return 12 rows._