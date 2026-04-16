---
title: "SELF JOIN"
---

## SELECT with a self JOIN

In this exercise we will query the _orders_ table using a self _JOIN_.

```
SELECT a.employee_id AS employee_id_1,
       b.employee_id AS employee_id_2,
       a.customer_id
FROM orders a,
     orders b
WHERE a.employee_id &lt;&gt; b.employee_id
  AND a.customer_id=b.customer_id
ORDER BY a.employee_id;
```
_The query should return 8,704 rows._