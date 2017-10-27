---
date: 2016-03-09T00:11:02+01:00
title: Test YugaByte CQL
weight: 7
---

After [creating a local cluster](/community-edition/quick-start/create-local-cluster/), follow the instructions below to test its CQL service.

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Apache Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It utilizes the Python CQL driver, and connects to the single node specified on the command line. For ease of use, the YugaByte DB container ships with the 3.10 version of cqlsh in its bin directory.

## Connect To YugaByte Using cqlsh

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#docker">
      <i class="fa fa-docker" aria-hidden="true"></i>
      <b>Docker</b>
    </a>
  </li>
  <li >
    <a data-toggle="tab" href="#macos">
      <i class="fa fa-apple" aria-hidden="true"></i>
      <b>macOS</b>
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#linux">
      <i class="fa fa-linux" aria-hidden="true"></i>
      <b>Linux</b>
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="docker" class="tab-pane fade in active">
    {{% includeMarkdown "community-edition/quick-start/docker/test-yugabyte-cql.md" /%}}
  </div>
  <div id="macos" class="tab-pane fade">
    {{% includeMarkdown "community-edition/quick-start/binary/macos-test-yugabyte-cql.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade">
    {{% includeMarkdown "community-edition/quick-start/binary/linux-test-yugabyte-cql.md" /%}}
  </div> 
</div>


## Create a table

Create a keyspace called 'myapp'.

```sql
cqlsh> CREATE KEYSPACE myapp;
```


Create a table named 'stock_market' which can store stock prices at various timestamps for different stock ticker symbols.

```sql
CREATE TABLE myapp.stock_market (
  stock_symbol text,
  ts timestamp,
  current_price float,
  PRIMARY KEY (stock_symbol, ts)
);
```



## Insert data

Let us insert some data for a few stock symbols into our newly created 'stock_market' table. You can copy-paste these values directly into your cqlsh shell.

```sql
// Insert some values for the AAPL stock symbol.
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 09:00:00.000000+0000',157.41);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 10:00:00.000000+0000',157);

// Next insert some values for the FB stock symbol.
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 09:00:00.000000+0000',170.63);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 10:00:00.000000+0000',170.1);

// Next insert some values for the GOOG stock symbol.
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 09:00:00.000000+0000',972.56);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 10:00:00.000000+0000',971.91);
```

## Query the table

Query all the values we have inserted into the database for the stock symbol 'AAPL' as follows.

```sql
cqlsh> SELECT * FROM myapp.stock_market WHERE stock_symbol = 'AAPL';

 stock_symbol | ts                              | current_price
--------------+---------------------------------+---------------
         AAPL | 2017-10-25 12:00:00.000000+0000 |        158.14
         AAPL | 2017-10-25 13:00:00.000000+0000 |     158.10001

(2 rows)
```


Query all the values for 'FB' and 'GOOG' as follows.

```sql
cqlsh> SELECT * FROM myapp.stock_market WHERE stock_symbol in ('FB', 'GOOG');

 stock_symbol | ts                              | current_price
--------------+---------------------------------+---------------
         GOOG | 2017-10-25 12:00:00.000000+0000 |     972.73999
         GOOG | 2017-10-25 13:00:00.000000+0000 |     973.51001
           FB | 2017-10-25 12:00:00.000000+0000 |     170.74001
           FB | 2017-10-25 13:00:00.000000+0000 |     171.50999

(4 rows)
```
