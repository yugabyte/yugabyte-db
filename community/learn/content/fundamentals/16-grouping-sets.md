---
title: "GROUPING SETS"
---

## SELECT with a GROUP BY and GROUPING SETS

In this exercise we will query the _suppliers_ table and group the _number\_of\_people_ results into _city_, _country_ and _contact\_title_ sets.

```
SELECT contact_title,
       count(contact_title) AS number_of_people,
       city,
       country
FROM suppliers
GROUP BY GROUPING
SETS (city,
      country,
      contact_title);
```

_This query should return 60 rows_