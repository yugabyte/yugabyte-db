---
title: "INTERSECT"
---

## SELECT with an INTERSECT

In this exercise we will query the _orders_ and _country_ tables and return the result sets that are found in both tables.

```
SELECT ship_country
FROM orders INTERSECT
SELECT country
FROM suppliers;
```

_This query should return 12 rows_

## SELECT with an INTERSECT AND ORDER BY

In this exercise we will query the _orders_ and _country_ tables, return the result sets that are found in both tables and oder them by _ship\_country_.

```
SELECT ship_country
FROM orders INTERSECT
SELECT country
FROM suppliers
ORDER BY ship_country;
```

_This query should return 12 rows_