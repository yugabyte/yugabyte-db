---
title: "ORDER BY"
---

## SELECT with an ascending ORDER BY

In this exercise we sort _employees_ by _first\_name_ in ascending order.

```
SELECT first_name,
       last_name,
       title,
       address,
       city
FROM employees
ORDER BY first_name ASC;
```

_The query should return 9 rows._

## SELECT with an descending ORDER BY

In this exercise we sort _employees_ by _first\_name_ in descending order.

```
SELECT first_name,
       last_name,
       title,
       address,
       city
FROM employees
ORDER BY first_name DESC;
```

_The query should return 9 rows._

## SELECT with a ascending and descending ORDER BYs

In this exercise we sort _employees_ by _first\_name_ in ascending order and then by last name in descending order.

```
SELECT first_name,
       last_name
FROM employees
ORDER BY first_name ASC,
         last_name DESC;
```

_The query should return 9 rows_