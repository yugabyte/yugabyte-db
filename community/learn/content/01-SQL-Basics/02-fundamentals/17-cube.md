---
title: "CUBE"
---

## SELECT with a GROUP BY and CUBE

In this exercise we will query the _products_ table, group the results and then generate multiple grouping sets from the results.

```
SELECT product_id, supplier_id, product_name, 
       SUM(units_in_stock)
FROM products
GROUP BY product_id, CUBE(supplier_id);
```

_This query should return 154 rows_

## SELECT with a GROUP BY and a partial CUBE

In this exercise we will query the _products_ table, group the results and then generate a subset of grouping sets from the results.

```
SELECT product_id, supplier_id, product_name, 
       SUM(units_in_stock)
FROM products
GROUP BY product_id, CUBE(product_id, supplier_id);
```

_This query should return 200 rows_