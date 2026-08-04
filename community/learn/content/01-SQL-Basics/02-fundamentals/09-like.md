---
title: "LIKE"
---

## SELECT with a LIKE operator

In this exercise we will query the _products_ table and find all the products whose names have the letter C in them.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_name LIKE '%C%'
```

_This query should return 17 rows_

## SELECT with a LIKE operator using % and _

In this exercise we will query the _products_ table and find all the products whose names have a single character before the letter E appears in them.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_name LIKE '_e%'
```

_This query should return 5 rows_

## SELECT with a NOT LIKE operator

In this exercise we will query the _products_ table and find all the products whose names do not start with the letter C.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_name NOT LIKE 'C%'
```

_This query should return 68 rows_

## SELECT with an ILIKE operator

In this exercise we will query the _products_ table using the ILIKE operator (which acts like the LIKE operator) to find the products that have the letter C in them. In addition, the ILIKE operator matches value case-insensitively.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_name ILIKE '%C%'
```

_This query should return 37 rows_