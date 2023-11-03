---
title: Primary keys in YugabyteDB YCQL
headerTitle: Primary keys
linkTitle: Primary keys
description: Defining Primary key constraint in YCQL
headContent: Explore primary keys in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: primary-key-ycql
    parent: explore-indexes-constraints
    weight: 201
aliases:
  - /preview/explore/ysql-language-features/constraints/
  - /preview/explore/indexes-constraints/constraints/
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../primary-key-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../primary-key-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The Primary Key constraint is a means to uniquely identify a specific row in a table via one or more columns. In YCQL, it should be defined either under the `column_constraint` or the `table_constraint`, but not under both:

- **column_constraint**: Columns can be either STATIC or declared as the PRIMARY KEY. Declaring a column as STATIC results in the same value being shared for all those rows that belong to the same partition (rows with the partition key). Declaring a column as a PRIMARY KEY makes that individual column its sole component.

- **table_constraint**: PRIMARY KEY defined as the table_constraint takes columns to form one or more partition keys and zero or more clustering keys. Syntactically, the order is to have the `partition_key_column_list` first, followed by the `clustering_key_column_list`.

Refer to the Grammar section for [CREATE TABLE](../../../api/ycql/ddl_create_table/#grammar) in YCQL. The [PRIMARY KEY](../../../api/ycql/ddl_create_table/#primary-key) section includes details about the [partition key](../../../api/ycql/ddl_create_table/#partition-key), [clustering key](../../../api/ycql/ddl_create_table/#clustering-key), and [STATIC COLUMNS](../../../api/ycql/ddl_create_table/#static-columns).

## Examples

{{% explore-setup-single %}}

### Column constraint

1. Create a `users` table with `user_id` as the primary key.

    ```cql
    CREATE TABLE users(user_id INT PRIMARY KEY, full_name TEXT);
    ```

1. Insert two rows into the table and check the entries.

    ```cql
    INSERT INTO users(user_id , full_name) VALUES (1, 'John');
    INSERT INTO users(user_id , full_name) VALUES (1, 'Rose');
    SELECT * FROM users;
    ```

    ```output
     user_id | full_name
    ---------+-----------
           1 |       Rose
    ```

The second entry with `Rose` as the `full_name` overrides the first entry because the `user_id` is the same.

### Table constraint

1. Create a `devices` table with `supplier_id` and `device_id` as the partitioning columns and `model` year as the clustering column.

    ```cql
    CREATE TABLE devices(supplier_id INT,
                        device_id INT,
                        model_year INT,
                        device_name TEXT,
                        PRIMARY KEY((supplier_id, device_id), model_year));

    ```

1. Insert three rows into the table and view the contents.

    ```cql
    INSERT INTO devices(supplier_id, device_id, device_name, model_year) VALUES (1, 101, 'iPhone', 2013);
    INSERT INTO devices(supplier_id, device_id, device_name, model_year) VALUES (1, 102, 'Pixel', 2011);
    INSERT INTO devices(supplier_id, device_id, device_name, model_year) VALUES (1, 102, 'Samsung S3', 2001);
    SELECT * FROM devices;
    ```

    ```output
     supplier_id | device_id | model_year | device_name
    -------------+-----------+------------+-------------
               1 |       101 |       2013 |      iPhone
               1 |       102 |       2001 |     Samsung
               1 |       102 |       2011 |       Pixel

    (3 rows)
    ```

1. Insert another entry with `supplier_id`, `device_id`, and `model_year` as 1, 102, and 2011 respectively.

    ```cql
    INSERT INTO devices(supplier_id, device_id, device_name, model_year) VALUES (1, 102, 'MotoRazr', 2011);
    SELECT * FROM devices;
    ```

    ```output
     supplier_id | device_id | model_year | device_name
    -------------+-----------+------------+-------------
               1 |       101 |       2013 |      iPhone
               1 |       102 |       2001 |     Samsung
               1 |       102 |       2011 |    MotoRazr
    ```

The row with `device_name` Pixel is replaced with MotoRazr.

## Explore more examples

- [Use column constraint to define a static column](../../../api/ycql/ddl_create_table/#use-column-constraint-to-define-a-static-column)
- [Example for clustering columns by ORDER](../../../api/ycql/ddl_create_table/#use-table-property-to-define-the-order-ascending-or-descending-for-clustering-columns)
- [Example to define the default expiration time for rows](../../../api/ycql/ddl_create_table/#use-table-property-to-define-the-default-expiration-time-for-rows)
- [Create a table specifying the number of tablets](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets)
