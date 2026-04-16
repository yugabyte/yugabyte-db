---
title: "HAVING"
---

## SELECT with a HAVING clause

In this exercise we will query the _products_ table, group the results and return only those that have a _category\_id_ of "5".

```
SELECT product_id,
       product_name,
       unit_price,
       category_id
FROM products
GROUP BY product_id,
         product_name,
         unit_price,
         category_id
HAVING category_id=5;
```

_This query should return 7 rows_

## SELECT with a HAVING clause and a function

In this exercise we will query the _products_ table, group the results and return only those _unit\_price_ when multiplied by _units\_in\_stock_ is greater than $2800.

```
SELECT product_name,
       sum(unit_price * units_in_stock),
       units_in_stock
FROM products
GROUP BY product_name,
         unit_price,
         units_in_stock
HAVING unit_price * units_in_stock > 2800;
```

_This query should return 7 rows_

## SELECT with a HAVING clause and COUNT

In this exercise we will query the _products_ table, group the results and return only those _unit\_price_ greater than 28.

```
SELECT product_name,
       sum(unit_price) units_in_stock
FROM products
GROUP BY product_name,
         unit_price,
         units_in_stock
HAVING unit_price > 28
```

_This query should return 26 rows_

## SELECT with a HAVING clause and a less than operator

In this exercise we will query the _products_ table, group the results and return only those whose _category\_id_ is less than 4.

```
SELECT product_id,
       product_name,
       unit_price,
       category_id
FROM products
GROUP BY product_id,
         product_name,
         unit_price,
         category_id
HAVING category_id < 4;
```

_This query should return 37 rows_