---
title: "ROLLUP"
---

## SELECT with a GROUP BY and ROLLUP

In this exercise we will query the _products_ table, group the results by _product\_id_ and then generate multiple grouping sets from the results using ROLLUP.

```
SELECT product_id,
       supplier_id,
       product_name,
       SUM(units_in_stock)
FROM products
GROUP BY product_id,
         ROLLUP(supplier_id);
```

_This query should return 154 rows_

## SELECT with a GROUP BY and PARTIAL ROLLUP

In this exercise we will query the _products_ table, group the results by _product\_id_ and then generate multiple grouping sets from the results using ROLLUP.

```
SELECT product_id,
       supplier_id,
       product_name,
       SUM(units_in_stock)
FROM products
GROUP BY product_id ROLLUP(supplier_id, product_id);
```

_This query should return TBD rows_