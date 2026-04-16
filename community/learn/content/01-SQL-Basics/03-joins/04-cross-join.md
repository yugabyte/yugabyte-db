---
title: "CROSS JOIN"
---

## SELECT with a CROSS JOIN

In this exercise we will query the _customers_ table using a _CROSS JOIN_ with the _suppliers_ table.

```
SELECT customers.customer_id,
       customers.contact_name,
       suppliers.company_name,
       suppliers.supplier_id
FROM customers
CROSS JOIN suppliers;
```
_The query should return 2,639 rows._