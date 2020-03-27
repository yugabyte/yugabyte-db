---
title: "GROUP BY"
---

## SELECT with a GROUP BY

In this exercise we will query the _products_ table and group the results by _product\_id_, then _product\_name_, and finally _unit\_price_.

```
SELECT product_id,
       product_name,
       unit_price
FROM products
GROUP BY product_id,
         product_name,
         unit_price;
```

_This query should return 77 rows_

## SELECT with a GROUP BY and a function

In this exercise we will query the _orders_ table and group the results by _employee\_id_, while only returning the total counts of _order\_id_.

```
SELECT count(order_id),
       employee_id
FROM orders
GROUP BY employee_id;
```

_This query should return 9 rows_

