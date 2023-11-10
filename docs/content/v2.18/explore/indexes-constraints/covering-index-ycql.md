---
title: Covering indexes in YugabyteDB YCQL
headerTitle: Covering indexes
linkTitle: Covering indexes
description: Using covering indexes in YCQL
headContent: Explore covering indexes in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.18:
    identifier: covering-index-ycql
    parent: explore-indexes-constraints
    weight: 255
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../covering-index-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../covering-index-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

A covering index is an index that includes all the columns required by a query, including columns that would typically not be a part of an index. This is done by using the INCLUDE keyword to list the columns you want to include.

A covering index is an efficient way to perform `index-only` scans, where you don't need to scan the table, just the index, to satisfy the query.

## Syntax

```cql
CREATE INDEX columnA_index_name ON table_name(columnA) INCLUDE (columnC);
```

For additional information on creating indexes, see [CREATE INDEX](../../../api/ycql/ddl_create_index/).

## Example

{{% explore-setup-single %}}

The following exercise demonstrates how to optimize query performance using a covering index.

1. Create a sample HR keyspace as follows:

    ```cql
    ycqlsh> CREATE KEYSPACE HR;
    ycqlsh> USE HR;
    ```

1. Create and insert some rows into a table `employees` with two columns `id` and `username`

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

1. Run a select query to fetch a row with a particular username

    ```sql
    SELECT name FROM employees WHERE department='Sales';
    ```

    ```output
     name
    -------------
     Bette Davis
    ```

1. Run `EXPLAIN` on select query to show that the query does a sequential scan before creating an index

    ```cql
    EXPLAIN SELECT name FROM employees WHERE department='Sales';
    ```

    ```output
     QUERY PLAN
     ----------------------------------
     Seq Scan on docs.employees
      Filter: (department = 'Sales')
    ```

1. Optimize the SELECT query by creating an index as follows

    ```cql
    CREATE INDEX index_employees_department ON employees(department);
    ```

    ```cql
    EXPLAIN SELECT name FROM employees WHERE department='Sales';
    ```

    ```output
     QUERY PLAN
     --------------------------------------------------------------------
     Index Scan using index_employees_department on employees
      Key Conditions: (department = 'Sales')
    ```

   As the select query includes a column that is not included in the index, the query still reaches out to the table to get the column values.

1. Create a covering index by specifying the username column in the INCLUDE clause as follows:

    ```sql
    CREATE INDEX index_employees_department_nm ON employees(department) include(name);
    ```

    A covering index allows you to perform an index-only scan if the query select list matches the columns that are included in the index and the additional columns added using the INCLUDE keyword.

    Ideally, specify columns that are updated frequently in the INCLUDE clause. For other cases, it is probably faster to index all the key columns.

    ```sql
    EXPLAIN SELECT name FROM employees WHERE department='Sales';
    ```

    ```output
     QUERY PLAN
    ----------------------------------------------------------------------------
    Index Only Scan using HR.index_employees_department_nm on HR.employees
      Key Conditions: (department = 'Sales')
    ```
