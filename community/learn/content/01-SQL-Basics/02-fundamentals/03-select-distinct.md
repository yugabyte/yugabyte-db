---
title: "SELECT DISTINCT"
---

## SELECT with DISTINCT on one column

In this exercise we query the _orders_ table and return only the distinct values in the _order\_id_ column.

```
SELECT DISTINCT order_id
FROM orders;
```

_The query should return 830 rows_

## SELECT with DISTINCT including multiple columns

In this exercise we query the _orders_ table and return only the distinct values based on combining the specified columns.

```
SELECT DISTINCT order_id,
                customer_id,
                employee_id
FROM orders;
```

_The query should return 830 rows_

## SELECT with DISTINCT ON expression

In this exercise we query the _orders_ table and ask to keep just the first row of each group of duplicates.

```
SELECT DISTINCT ON (employee_id)employee_id AS id_number,
                   customer_id
FROM orders
ORDER BY id_number,
         customer_id;
```

_The query should return 9 rows_