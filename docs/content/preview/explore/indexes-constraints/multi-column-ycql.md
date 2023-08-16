---
title: Muti-column indexes in YugabyteDB YCQL
headerTitle: Multi-column indexes
linkTitle: Multi-column indexes
description: Using Multi-column indexes in YCQL
headContent: Explore Multi-column indexes in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: multi-column-index-ycql
    parent: explore-indexes-constraints
    weight: 241
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../multi-column-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../multi-column-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Multi-column indexes are also known as compound indexes using which you can create an index with multiple columns.
Multi-column indexes are similar to standard indexes as they both store a sorted table of pointers to data entries. These indexes provide faster access to data entries as it uses multiple columns to sort through data faster.

## Syntax

To add a multi-column index to an existing table, you can use the following syntax:

```sql
CREATE INDEX index_name ON table_name(col2,col3,col4);
```

{{< note >}}

The column order is very important when you create a multi-column index in YCQL because of the structure in which the index is stored. As such, these indexes have a hierarchical order from left to right. So, for the preceding syntax, you can perform search using the following column combinations:

```sql
(col2)
(col2,col3)
(col2,col3,col4)
```

A column combination like (col2,col4) cannot be used to search or query a table.

{{< /note >}}

## Example

{{% explore-setup-single %}}

Create a keyspace and a table as follows:

```cql
ycqlsh> CREATE KEYSPACE example;
ycqlsh> USE example;
ycqlsh:example>
CREATE TABLE employees (
    employee_id int PRIMARY KEY,
    first_name text,
    last_name text,
    dept_name text)
WITH TRANSACTIONS = {'enabled': 'true'};
```

Insert data to the employees table as follows:

```cql
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(1223, 'Lucille', ' Ball', 'Operations');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(1433, 'Brett', ' Davis', 'Marketing');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(1439, 'Simon', ' Thompson', 'HR');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(7898, 'Jackson', ' Lee', 'Sales');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(7866, 'Dan', ' Shen', 'Marketing');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(5666, 'Rob', ' Mencey', 'Marketing');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(4321, 'Dave', ' Spencer', 'Operations');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(3214, 'Dave', ' Marley', 'Operations');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(4121, 'Leo', ' Marvin', 'Sales');
ycqlsh:example> INSERT INTO employees(employee_id, first_name, last_name, dept_name) VALUES(4021, 'Jackson', ' Bob', 'Marketing');
```

View the contents of the `employees` table:

```cql
ycqlsh:example> select * from employees limit 2;
```

```output
 employee_id | first_name | last_name | dept_name
-------------+------------+-----------+------------
        3214 |       Dave |    Marley | Operations
        1223 |    Lucille |      Ball | Operations
```

Suppose you want to query the subset of employees by their first and last names. The query plan using the `EXPLAIN` statement would look like the following:

```cql
ycqlsh:example> EXPLAIN SELECT * FROM employees WHERE first_name='Dave' AND last_name='Marley';
```

```output
 QUERY PLAN
------------------------------------------------------------
 Seq Scan on example.employees
   Filter: (first_name = 'Dave') AND (last_name = 'Marley')
```

To process the preceding query, the whole `employees` table needs to be scanned and all the shards have to be accessed. For large organizations, this will take a significant amount of time as the query is executed sequentially as demonstrated in the preceding EXPLAIN statement.

To speed up the process, you can create an index for the department column, as follows:

```cql
ycqlsh:example> Create INDEX index_name ON employees2(first_name, last_name);
```

The following example executes the query after the index has been applied to the columns `first_name` and `last_name` and uses the EXPLAIN statement to prove that the index participated in the processing of the query:

```cql
EXPLAIN SELECT * FROM employees WHERE first_name='Dave' AND last_name='Marley';
```

```output
 QUERY PLAN
-----------------------------------------------------------
 Index Scan using example.index_name on example.employees2
   Key Conditions: (first_name = 'Dave')
   Filter: (last_name = 'Marley')
```
