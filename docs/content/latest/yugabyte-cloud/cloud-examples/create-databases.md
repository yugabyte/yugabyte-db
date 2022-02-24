<!---
title: Create and explore a database
linkTitle: Create a database
description: Create and explore a database using YSQL.
headcontent:
image: /images/section_icons/quick_start/explore_ysql.png
aliases:
  - /latest/deploy/yugabyte-cloud/create-databases/
  - /latest/yugabyte-cloud/create-databases/
menu:
  latest:
    parent: cloud-develop
    name: Create a database
    identifier: create-databases-1-ysql
    weight: 600
type: page
isTocNested: true
showAsideToc: true
--->

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../create-databases/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

 <li >
    <a href="../create-databases-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

Using the `ysqlsh` shell, you can interact with your YugabyteDB database using the YSQL API. In the following exercise, you'll use ysqlsh to create a database, load a sample dataset, and run a simple query.

Your YugabyteDB client shell installation includes sample datasets you can use to test out YugabyteDB. These are located in the `share` directory. The datasets are provided in the form of SQL script files. (The datasets are also available in the [sample directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample)). This exercise uses the [Retail Analytics](../../../sample-data/retail-analytics/) dataset.

The following files are used:

- [schema.sql](https://github.com/yugabyte/yugabyte-db/tree/master/sample/schema.sql) — Database schema, creates tables and other database objects
- [orders.sql](https://github.com/yugabyte/yugabyte-db/tree/master/sample/orders.sql) — Orders table
- [products.sql](https://github.com/yugabyte/yugabyte-db/tree/master/sample/products.sql) — Products table
- [reviews.sql](https://github.com/yugabyte/yugabyte-db/tree/master/sample/reviews.sql) — Reviews table
- [users.sql](https://github.com/yugabyte/yugabyte-db/tree/master/sample/users.sql) — Users table

## Create a database and load the dataset

To create a database and load the Retail Analytics dataset, do the following:

1. Connect to your cluster using `ysqlsh` using the [Client Shell](../connect-client-shell/) from your computer.

1. Create a database (`yb_demo`) using the `CREATE DATABASE` command.

    ```sql
    yugabyte=# CREATE DATABASE yb_demo;
    ```

1. Connect to the new database using the YSQL shell `\c` meta command.

    ```sql
    yugabyte=# \c yb_demo;
    ```

1. Create the database schema, which includes four tables, by running the `\i` meta command.

    ```sql
    \i share/schema.sql;
    ```

1. Load the data into the tables by running the following four `\i` commands:

    ```sql
    \i share/products.sql;
    \i share/users.sql;
    \i share/orders.sql;
    \i share/reviews.sql;
    ```

    You now have sample data and are ready to begin exploring YSQL in YugabyteDB.

## Run a simple query

To look at the schema of the `products` table, enter the following command:

```sql
\d products
```

```output
                                        Table "public.products"
   Column   |            Type             | Collation | Nullable |               Default                
------------+-----------------------------+-----------+----------+--------------------------------------
 id         | bigint                      |           | not null | nextval('products_id_seq'::regclass)
 created_at | timestamp without time zone |           |          | 
 category   | text                        |           |          | 
 ean        | text                        |           |          | 
 price      | double precision            |           |          | 
 quantity   | integer                     |           |          | 5000
 rating     | double precision            |           |          | 
 title      | text                        |           |          | 
 vendor     | text                        |           |          | 
Indexes:
    "products_pkey" PRIMARY KEY, lsm (id HASH)
```

Run a query to select the `id`, `title`, `category` and `price` columns for the first five products as follows:

```sql
SELECT id, title, category, price, rating
          FROM products
          LIMIT 5;
```

```output
 id  |           title            | category |      price       | rating 
-----+----------------------------+----------+------------------+--------
  22 | Enormous Marble Shoes      | Gizmo    | 21.4245199604423 |    4.2
  38 | Lightweight Leather Gloves | Gadget   | 44.0462485589292 |    3.8
 162 | Gorgeous Copper Knife      | Gadget   | 22.3785988001101 |    3.3
 174 | Rustic Iron Keyboard       | Gadget   | 74.4095392945406 |    4.4
  46 | Rustic Linen Keyboard      | Gadget   | 78.6996782532274 |      4
(5 rows)
```

## More to explore

To explore more of the Retail Analytics database, refer to the exercises in [Retail Analytics](../../../sample-data/retail-analytics/).

Here are links to documentation on the tested datasets and the steps to create the other sample databases included with YugabyteDB:

- [Northwind](../../../sample-data/northwind/)
- [PgExercises](../../../sample-data/pgexercises/)
- [SportsDB](../../../sample-data/sportsdb/)
- [Chinook](../../../sample-data/chinook/)

For information on YSQL and the ysqlsh shell:

- [ysqlsh](../../../admin/ysqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YSQL API](../../../api/ysql/) — Reference for supported YCQL statements, data types, functions, and operators.

## Next steps

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Connect an application](../connect-applications/)
