---
title: "BETWEEN"
---

## SELECT with BETWEEN

In this exercise we will query the _products_ table and find all the products who have a _product\_id_ between 10 and 20.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_id BETWEEN 10 AND 20
```

_This query should return 11 rows_

## SELECT with NOT BETWEEN

In this exercise we will query the _products_ table and find all the products who have a _product\_id_ that is not between 10 and 20.

```
SELECT product_id,
       product_name,
       quantity_per_unit
FROM products
WHERE product_id NOT BETWEEN 10 AND 20
```

_This query should return 66 rows_