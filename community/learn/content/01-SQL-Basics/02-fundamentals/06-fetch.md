---
title: "FETCH"
---

## SELECT with FETCH and ORDER BY clauses

In this exercise we'll query the _customers_ table and order the results by _company\_name_ in ascending order, while limiting the rows returned to 7.

```
SELECT customer_id,
       company_name,
       contact_name,
       contact_title
FROM customers
ORDER BY company_name ASC FETCH NEXT 7 ROWS ONLY;
```

_This query should return 7 rows._

## SELECT with FETCH, OFFSET and ORDER BY clauses

In this exercise we'll query the _customers_ table and order the results by _company\_name_ in ascending order, while limiting the rows returned to the 7 that come after the first 2.

```
SELECT customer_id,
       company_name,
       contact_name,
       contact_title
FROM customers
ORDER BY company_name ASC
OFFSET 2 FETCH NEXT 7 ROWS ONLY;
```

_This query should return 7 rows._