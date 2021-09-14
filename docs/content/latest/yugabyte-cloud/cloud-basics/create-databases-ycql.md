---
title: Create and explore a database
headerTitle: Create and explore a database
linkTitle: Create a database
description: Create and explore a database using YCQL.
headcontent:
image: /images/section_icons/quick_start/explore_ycql.png
menu:
  latest:
    parent: cloud-basics
    name: Create a database
    identifier: create-databases-2-ycql
    weight: 60
type: page
isTocNested: false
showAsideToc: true
---

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

## Create a database and load the dataset

To create a database and load the Retail Analytics dataset, do the following:

1. Connect to your cluster using `ycqlsh` using the [Client Shell](../connect-to-clusters#connect-via-client-shell) from your computer.

1. Create a keyspace called 'myapp'.

    ```sql
    ycqlsh> CREATE KEYSPACE myapp;
    ```

1. Create a table named `stock_market`, which can store stock prices at various timestamps for different stock ticker symbols.

    ```sql
    ycqlsh> CREATE TABLE myapp.stock_market (
      stock_symbol text,
      ts text,
      current_price float,
      PRIMARY KEY (stock_symbol, ts)
    );
    ```

## Insert data

Insert some data for a few stock symbols into the `stock_market` table. You can copy and paste these values directly into your ycqlsh shell.

```sql
ycqlsh> INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 09:00:00',157.41);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 10:00:00',157);
```

```sql
ycqlsh> INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 09:00:00',170.63);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 10:00:00',170.1);
```

```sql
ycqlsh> INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 09:00:00',972.56);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 10:00:00',971.91);
```

## Query the table

Query all the values you have inserted into the database for the stock symbol 'AAPL' as follows:

```sql
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

```sql
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

## Next steps

- [Add database users](../add-users/)
- [Connect a YCQL Java application](../../cloud-develop/connect-ycql-application)
