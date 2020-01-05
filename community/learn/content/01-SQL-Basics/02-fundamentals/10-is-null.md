---
title: "IS NULL"
---

## SELECT with a IS NULL operator

In this exercise we will query the _customers_ table and find all the customers who do not have a region assigned to them.

```
SELECT contact_name,
       contact_title,
       city,
       country,
       region
FROM customers
WHERE region IS NULL;
```

_This query should return 60 rows_

## SELECT with a IS NOT NULL operator

In this exercise we will query the _customers_ table and find all the customers who do have a region assigned to them.

```
SELECT contact_name,
       contact_title,
       city,
       country,
       region
FROM customers
WHERE region IS NOT NULL;
```

_This query should return 31 rows_
