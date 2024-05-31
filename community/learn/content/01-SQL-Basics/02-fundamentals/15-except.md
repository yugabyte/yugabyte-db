---
title: "EXCEPT"
---

## SELECT with an EXCEPT

In this exercise we will query the _orders_ and _country_ tables and return the distinct rows from the first query that are not in the output of the second.

```
SELECT ship_country
FROM orders
EXCEPT
SELECT country
FROM suppliers;
```

_This query should return 9 rows_