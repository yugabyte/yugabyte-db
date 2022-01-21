<!---
title: Create and explore a database
linkTitle: Create a database
description: Use distributed CQL to explore core features of YugabyteDB.
headcontent:
image: /images/section_icons/quick_start/explore_ycql.png
menu:
  latest:
    parent: cloud-develop
    name: Create a database
    identifier: create-databases-2-ycql
    weight: 600
type: page
isTocNested: false
showAsideToc: true
--->

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../create-databases/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

 <li >
    <a href="../create-databases-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Using the `ycqlsh` shell, you can interact with your YugabyteDB database using the YCQL API. In the following exercise, you'll use ycqlsh to create a keyspace, add a table, insert some data, and run a simple query.

## Create a keyspace and add a table

To create a keyspace and add a table, do the following:

1. Connect to your cluster using `ycqlsh` using the [Client Shell](../connect-client-shell/) from your computer. You can also do this exercise from the [Cloud Shell](../connect-cloud-shell/).

1. Create a keyspace called 'myapp'.

    ```cql
    ycqlsh> CREATE KEYSPACE myapp;
    ```

1. Create a table named `stock_market`, which can store stock prices at various timestamps for different stock ticker symbols.

    ```cql
    ycqlsh> CREATE TABLE myapp.stock_market (
      stock_symbol text,
      ts text,
      current_price float,
      PRIMARY KEY (stock_symbol, ts)
    );
    ```

## Insert data

Insert some data for a few stock symbols into the `stock_market` table. You can copy and paste these values directly into your ycqlsh shell.

```cql
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 09:00:00',157.41);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 10:00:00',157);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 09:00:00',170.63);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 10:00:00',170.1);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 09:00:00',972.56);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 10:00:00',971.91);
```

## Query the table

Query all the values you have inserted into the database for the stock symbol 'AAPL' as follows:

```cql
ycqlsh> SELECT * FROM myapp.stock_market WHERE stock_symbol = 'AAPL';
```

```output
 stock_symbol | ts                  | current_price
--------------+---------------------+---------------
         AAPL | 2017-10-26 09:00:00 |        157.41
         AAPL | 2017-10-26 10:00:00 |           157

(2 rows)
```

Query all the values for `FB` and `GOOG` as follows:

```cql
ycqlsh> SELECT * FROM myapp.stock_market WHERE stock_symbol in ('FB', 'GOOG');
```

```output
 stock_symbol | ts                  | current_price
--------------+---------------------+---------------
           FB | 2017-10-26 09:00:00 |        170.63
           FB | 2017-10-26 10:00:00 |     170.10001
         GOOG | 2017-10-26 09:00:00 |        972.56
         GOOG | 2017-10-26 10:00:00 |     971.90997

(4 rows)
```

## Learn more

- [ycqlsh](../../../admin/ycqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YCQL API](../../../api/ycql/) — Reference for supported YCQL statements, data types, functions, and operators.

## Next steps

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Connect a YCQL Java application](../../cloud-develop/connect-ycql-application/)
