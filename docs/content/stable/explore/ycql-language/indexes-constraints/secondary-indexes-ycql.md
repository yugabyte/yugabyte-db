---
title: Secondary indexes in YugabyteDB YCQL
headerTitle: Secondary indexes
linkTitle: Secondary indexes
description: Overview of Secondary indexes in YCQL
headContent: Explore secondary indexes in YugabyteDB using YCQL
menu:
  stable:
    identifier: secondary-indexes-ycql
    parent: explore-indexes-constraints-ycql
    weight: 220
type: docs
---

Using indexes enhances database performance by enabling the database server to find rows faster. You can create, drop, and list indexes, as well as use indexes on expressions.

## Create indexes

You can create indexes in YCQL using the `CREATE INDEX` statement using the following syntax:

```sql
CREATE INDEX index_name ON table_name(column_list);
```

YCQL supports [Unique](../unique-index-ycql/), [Partial](../partial-index-ycql/), [Covering](../covering-index-ycql/), and [Multi-column](#multi-column-index) secondary indexes.

For more information, see [CREATE INDEX](../../../../api/ycql/ddl_create_index/).

## List indexes and verify the query plan

You can use the [DESCRIBE INDEX](../../../../admin/ycqlsh/#describe) command to check the indexes as follows:

```cql
DESCRIBE INDEX <index_name>
```

For more information, see [DESCRIBE INDEX](../../../../admin/ycqlsh/#describe).

You can also use the `EXPLAIN` statement to check if a query uses an index and determine the query plan before execution.

For more information, see [EXPLAIN](../../../../api/ycql/explain/).

## Remove indexes

You can remove an index using the `DROP INDEX` statement in YCQL using the following syntax:

```sql
DROP INDEX index_name;
```

For more information, see [DROP INDEX](../../../../api/ycql/ddl_drop_index/).

## Example

{{% explore-setup-single %}}

Suppose you work with a database that includes the following table populated with data:

```cql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text
)
WITH TRANSACTIONS = {'enabled':'true'};
```

```cql
INSERT INTO employees(employee_no, name,department) VALUES(1221, 'John Smith', 'Marketing');
INSERT INTO employees(employee_no, name,department) VALUES(1222, 'Bette Davis', 'Sales');
INSERT INTO employees(employee_no, name,department) VALUES(1223, 'Lucille Ball', 'Operations');
```

The following example shows a query that finds employees working in Operations:

```cql
SELECT * FROM employees WHERE department = 'Operations';
```

To process the preceding query, the whole `employees` table needs to be scanned and all the shards have to be accessed. For large organizations, this will take a significant amount of time. You can confirm this using the following `EXPLAIN` statement which indicates `seq scan` on the table.

```cql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

```output
 QUERY PLAN
---------------------------------------
 Seq Scan on employees
   Filter: (department = 'Operations')
```

To speed up the process, you create an index for the department column, as follows:

```cql
CREATE INDEX index_employees_department ON employees(department);
```

The following example executes the query after the index has been applied to `department` and uses the `EXPLAIN` statement to prove that the index is used during query execution:

```cql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

Following is the output produced by the preceding example:

```output
 QUERY PLAN
--------------------------------------------------------------------
 Index Scan using docs.index_employees_department on docs.employees
   Key Conditions: (department = 'Operations')
```

To remove the index `index_employees_department`, use the following command:

```cql
DROP INDEX index_employees_department;
```

## Multi-column index

Multi-column indexes can be beneficial in situations where queries are searching in more than a single column.

To add a multi-column index to an existing table, you can use the following syntax:

```sql
CREATE INDEX index_name ON table_name(col2,col3,col4);
```

The column order is very important when you create a multi-column index in YCQL because of the structure in which the index is stored. As such, these indexes have a hierarchical order from left to right. So, for the preceding syntax, you can perform search using the following column combinations:

```sql
(col2)
(col2,col3)
(col2,col3,col4)
```

A column combination like (col2,col4) cannot be used to search or query a table.

## Multi-column example

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

To speed up the process, you can create an index for `first_name` and `last_name` columns, as follows:

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

## Learn more

- [Secondary indexes with JSONB](../secondary-indexes-with-jsonb-ycql/)
