---
title: Unique indexes in YugabyteDB YCQL
headerTitle: Unique indexes
linkTitle: Unique indexes
description: Using Unique indexes in YCQL
headContent: Explore unique indexes in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  stable:
    identifier: unique-index-ycql
    parent: explore-indexes-constraints
    weight: 231
type: docs
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../unique-index-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../unique-index-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

If you need values in some of the columns to be unique, you can specify your index as `UNIQUE`.

When a unique index is applied to two or more columns, the combined values in these columns can't be duplicated in multiple rows. Note that because a `NULL` value is treated as a distinct value, you can have multiple `NULL` values in a column with a unique index.

If a table has a primary key defined, a corresponding unique index is created automatically.

{{% explore-setup-single %}}

## Syntax

```sql
CREATE UNIQUE INDEX index_name ON table_name(column_list);
```

## Example

1. Create a keyspace and a table as follows:

    ```cql
    ycqlsh> CREATE KEYSPACE yb_demo;
    ycqlsh> USE yb_demo;
    ycqlsh> CREATE TABLE employees(employee_no integer,name text,department text, PRIMARY KEY(employee_no))
            WITH transactions = {'enabled': 'true'};
    ```

1. Create a `UNIQUE` index for the `name` column in the `employees` table to allow only unique names in your table.

    ```cql
    CREATE UNIQUE INDEX index_employee_name ON employees(name);
    ```

1. Use the [DESCRIBE INDEX](/preview/admin/ycqlsh/#describe) command to verify the index creation.

    ```cql
    ycqlsh:yb_demo> DESCRIBE INDEX index_employee_name;
    ```

    ```output
    CREATE UNIQUE INDEX index_employee_name ON yb_demo.employees (name) INCLUDE (employee_no)
        WITH transactions = {'enabled': 'true'};
    ```

1. Insert values into the table and verify that no duplicate names are created.

    ```cql
    ycqlsh:yb_demo> INSERT INTO employees(employee_no, name, department) VALUES (1, 'John', 'Sales');
    ycqlsh:yb_demo> INSERT INTO employees(employee_no, name, department) VALUES (2, 'Bob', 'Marketing');
    ycqlsh:yb_demo> INSERT INTO employees(employee_no, name, department) VALUES (3, 'Bob', 'Engineering');
    ```

    ```output
    InvalidRequest: Error from server: code=2200 [Invalid query] message="Execution Error. Duplicate value disallowed by unique index index_employee_name
    INSERT INTO employees(employee_no, name, department) VALUES (3, 'Bob', 'Engineering');
           ^^^^
     (ql error -300)"
    ```

## Learn more

[CREATE INDEX](../../../api/ycql/ddl_create_index/)
