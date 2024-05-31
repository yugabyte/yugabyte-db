Chapter 4 Queries
---

The following queries are supported:

 - DUAL Table

### 4.1 DUAL Table
DUAL table is a virtual table provided by the system. 
Use when executing SQL where access to a base table is not required, 
such as when performing tests to get result expressions such as functions and operators.

**Example**

----

In the following example, the current system date is returned.

~~~
SELECT  CURRENT_DATE  "date" FROM DUAL;
    date
------------
 2013-05-14
(1 row)
~~~

----
