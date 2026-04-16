---
title: "WHERE"
---

## WHERE clause with an equal = operator

In this exercise we'll query the _orders_ table and return just the rows that equal the _employee\_id_ of 8.

```
SELECT order_id,
       customer_id,
       employee_id,
       order_date
FROM orders
WHERE employee_id=8;
```

_This query should return 104 rows._

## WHERE clause with an AND operator

In this exercise we'll query the _orders_ table and return just the rows that have an _employee\_id_ of 8 and a _customer\_id_ equal to "FOLKO".

```
SELECT order_id,
       customer_id,
       employee_id,
       order_date
FROM orders
WHERE employee_id=8
  AND customer_id= 'FOLKO';
```

_This query should return 6 rows._

## WHERE clause with an OR operator

In this exercise we'll query the _orders_ table and return just the rows that have an _employee\_id_ of 2 or 1.

```
SELECT order_id,
       customer_id,
       employee_id,
       order_date
FROM orders
WHERE employee_id=2
  OR employee_id=1;
```

_This query should return 219 rows._

## WHERE clause with an IN operator

In this exercise we'll query the _order\_ details_ table and return just the rows with an _order\_id_ of 10360 or 10368.

```
SELECT *
FROM order_details
WHERE order_id IN (10360,
                   10368);
```

_This query should return 9 rows._

## WHERE clause with a LIKE operator

In this exercise we'll query the _customers_ table and return just the rows that have a _company_name_ that starts with the letter "F".

```
SELECT customer_id,
       company_name,
       contact_name,
       city
FROM customers
WHERE company_name LIKE 'F%';
```

_This query should return 8 rows._

## WHERE clause with a BETWEEN operator

In this exercise we'll query the _orders_ table and return just the rows that have an _order\_id_ between 10,985 and 11,000.

```
SELECT order_id,
       customer_id,
       employee_id,
       order_date,
       ship_postal_code
FROM orders
WHERE order_id BETWEEN 10985 AND 11000;
```

_This query should return 16 rows._

## WHERE clause with a not equal &lt;&gt; operator

In this exercise we'll query the _employees_ table and return just the rows where the _employee\_id_ is not eqal to "1".

```
SELECT first_name,
       last_name,
       title,
       address,
       employee_id
FROM employees
WHERE employee_id &lt;&gt; 1;
```

_This query should return 8 rows._