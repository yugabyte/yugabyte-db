---
title: Presto
linkTitle: Presto
description: Presto
aliases:
  - /develop/ecosystem-integrations/presto/
menu:
  latest:
    identifier: presto
    parent: ecosystem-integrations
    weight: 575
---

[Presto](http://https://prestodb.io/) is a distributed SQL query engine optimized for ad-hoc analysis at interactive speed. It supports standard ANSI SQL, including complex queries, aggregations, joins, and window functions. It has a connector architecture to query data from many data sources.
This page shows how Presto can be setup to query YugaByte DB's YCQL tables.

## 1. Start Local Cluster

Follow [Quick Start](../../../quick-start/) instructions to run a local YugaByte DB cluster. Test YugaByte DB's Cassandra compatible API as [documented](../../../quick-start/test-cassandra/) so that you can confirm that you have a Cassandra compatible service running on `localhost:9042`. We assume you have created the keyspace and table, and inserted sample data as described there.

## 2. Download and Configure Presto
Detailed steps are documented [here](https://prestodb.io/docs/current/installation/deployment.html).
The following are the minimal setup steps for getting started.

```{.sh .copy .separator-dollar}
$ wget https://repo1.maven.org/maven2/com/facebook/presto/presto-server/0.212/presto-server-0.212.tar.gz
```

```{.sh .copy .separator-dollar}
$ tar xvf presto-server-0.212.tar.gz
```

```{.sh .copy .separator-dollar}
$ cd presto-server-0.212
```

### Create the “etc”, “etc/catalog”, and “data” directory inside the installation directory.

```{.sh .copy .separator-dollar}
$ mkdir etc
```

```{.sh .copy .separator-dollar}
$ mkdir etc/catalog
```

```{.sh .copy .separator-dollar}
$ mkdir data
```

### Create node.properties file - replace &lt;username&gt; below
```{.sh .copy .separator-dollar}
$ cat > etc/node.properties
```
```sh
node.environment=test
node.id=ffffffff-ffff-ffff-ffff-ffffffffffff
node.data-dir=/Users/<username>/presto-server-0.212/data
```

### Create jvm.config file
```{.sh .copy .separator-dollar}
$ cat > etc/jvm.config
```
```sh
-server
-Xmx6G
-XX:+UseG1GC
-XX:G1HeapRegionSize=32M
-XX:+UseGCOverheadLimit
-XX:+ExplicitGCInvokesConcurrent
-XX:+HeapDumpOnOutOfMemoryError
-XX:+ExitOnOutOfMemoryError
```

### Create config.properties file
```{.sh .copy .separator-dollar}
$ cat > etc/config.properties
```
```sh
coordinator=true
node-scheduler.include-coordinator=true
http-server.http.port=8080
query.max-memory=4GB
query.max-memory-per-node=1GB
discovery-server.enabled=true
discovery.uri=http://localhost:8080
```

### Create log.properties file
```{.sh .copy .separator-dollar}
$ cat > etc/log.properties
```
```sh
com.facebook.presto=INFO
```

### Configure Cassandra connector to Yugabyte DB

Create the cassandra catalog properties file in etc/catalog directory.
Detailed instructions are [here](https://prestodb.io/docs/current/connector/cassandra.html).

```{.sh .copy .separator-dollar}
$ cat > etc/catalog/cassandra.properties
```
```sh
connector.name=cassandra
cassandra.contact-points=127.0.0.1
```

## 3. Download Presto CLI

```{.sh .copy .separator-dollar}
$ cd ~/presto-server-0.212/bin
```
```{.sh .copy .separator-dollar}
$ wget https://repo1.maven.org/maven2/com/facebook/presto/presto-cli/0.212/presto-cli-0.212-executable.jar
```

Rename jar to ‘presto’. It is meant to be a self-running binary.

```{.sh .copy .separator-dollar}
$ mv presto-cli-0.212-executable.jar presto && chmod +x presto
```


## 4. Launch Presto server

```{.sh .copy .separator-dollar}
$ cd presto-server-0.212
```

To run in foreground mode.
```{.sh .copy .separator-dollar}
$ ./bin/launcher run       
```

To run in background mode.
```{.sh .copy .separator-dollar}
$ ./bin/launcher start  
```

## 5. Test Presto queries

Use the presto CLI to run ad-hoc queries. 

```{.sh .copy .separator-dollar}
$ ./bin/presto --server localhost:8080 --catalog cassandra --schema default
```

Start using `myapp`.

```{.sql .copy .separator-gt}
presto:default> use myapp;
```
```sh
USE
```

Show the tables available.

```{.sql .copy .separator-gt}
presto:myapp> show tables;
```
```
 Table
-------
 stock_market
(1 row)
```

Describe a particular table.

```{.sql .copy .separator-gt}
presto:myapp> describe stock_market;
```
```sh
    Column     |  Type   | Extra | Comment 
---------------+---------+-------+---------
 stock_symbol  | varchar |       |         
 ts            | varchar |       |         
 current_price | real    |       |         
(3 rows)
```

### Query with filter

```{.sql .copy .separator-gt}
presto:myapp> select * from stock_market where stock_symbol = 'AAPL';
```
```sh
 stock_symbol |         ts          | current_price 
--------------+---------------------+---------------
 AAPL         | 2017-10-26 09:00:00 |        157.41 
 AAPL         | 2017-10-26 10:00:00 |         157.0 
(2 rows)
```

### Query with aggregates

```{.sql .copy .separator-gt}
presto:myapp> select stock_symbol, avg(current_price) from stock_market group by stock_symbol;
```
```sh
 stock_symbol |  _col1  
--------------+---------
 GOOG         | 972.235 
 AAPL         | 157.205 
 FB           | 170.365 
(3 rows)
```

