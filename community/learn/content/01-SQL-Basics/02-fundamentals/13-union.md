---
title: "UNION"
---

## SELECT with a UNION

In this exercise we will query the _suppliers_ and _customers_ table and combine the result sets while removing duplicates it finds.

```
SELECT company_name
FROM suppliers
UNION
SELECT company_name
FROM customers;
```

_This query should return 120 rows_

## SELECT with a UNION ALL

In this exercise we will query the _suppliers_ and _customers_ tables and combine the result sets without removing duplicates if they exist.

```
SELECT company_name
FROM suppliers
UNION ALL
SELECT company_name
FROM customers;
```

_This query should return 120 rows_

## SELECT with a UNION and ORDER BY

In this exercise we will query the _suppliers_ and _customers_ tables, combine the result sets without removing duplicates if they exist and ordering them in descending order.

```
SELECT company_name
FROM suppliers
UNION ALL
SELECT company_name
FROM customers
ORDER BY company_name DESC
```

_This query should return 120 rows_