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
isTocNested: true
showAsideToc: true
---

[Presto](http://https://prestodb.io/) is a distributed SQL query engine optimized for ad-hoc analysis at interactive speed. It supports standard ANSI SQL, including complex queries, aggregations, joins, and window functions. It has a connector architecture to query data from many data sources.
This page shows how Presto can be setup to query YugaByte DB's YCQL tables.

## 1. Start Local Cluster

Follow [Quick Start](../../../quick-start/) instructions to run a local YugaByte DB cluster. Test YugaByte DB's Cassandra compatible API as [documented](../../../quick-start/test-cassandra/) so that you can confirm that you have a Cassandra compatible service running on `localhost:9042`. We assume you have created the keyspace and table, and inserted sample data as described there.

## 2. Download and Configure Presto
Detailed steps are documented [here](https://prestodb.io/docs/current/installation/deployment.html).
The following are the minimal setup steps for getting started.
<div class='copy separator-dollar'>
```sh
$ wget https://repo1.maven.org/maven2/com/facebook/presto/presto-server/0.212/presto-server-0.212.tar.gz
```
</div>
<div class='copy separator-dollar'>
```sh
$ tar xvf presto-server-0.212.tar.gz
```
</div>
<div class='copy separator-dollar'>
```sh
$ cd presto-server-0.212
```
</div>

### Create the “etc”, “etc/catalog”, and “data” directory inside the installation directory.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ mkdir etc
```
</div>
<div class='copy separator-dollar'>
```sh
$ mkdir etc/catalog
```
</div>
<div class='copy separator-dollar'>
```sh
$ mkdir data
```
</div>

### Create node.properties file - replace &lt;username&gt; below

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cat > etc/node.properties
```
</div>
```sh
node.environment=test
node.id=ffffffff-ffff-ffff-ffff-ffffffffffff
node.data-dir=/Users/<username>/presto-server-0.212/data
```

### Create jvm.config file

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cat > etc/jvm.config
```
</div>
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

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cat > etc/config.properties
```
</div>
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

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cat > etc/log.properties
```
</div>
```sh
com.facebook.presto=INFO
```

### Configure Cassandra connector to Yugabyte DB

Create the cassandra catalog properties file in etc/catalog directory.
Detailed instructions are [here](https://prestodb.io/docs/current/connector/cassandra.html).
<div class='copy separator-dollar'>
```sh
$ cat > etc/catalog/cassandra.properties
```
</div>
```sh
connector.name=cassandra
cassandra.contact-points=127.0.0.1
```

## 3. Download Presto CLI

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cd ~/presto-server-0.212/bin
```
</div>
<div class='copy separator-dollar'>
```sh
$ wget https://repo1.maven.org/maven2/com/facebook/presto/presto-cli/0.212/presto-cli-0.212-executable.jar
```
</div>

Rename jar to ‘presto’. It is meant to be a self-running binary.
<div class='copy separator-dollar'>
```sh
$ mv presto-cli-0.212-executable.jar presto && chmod +x presto
```
</div>


## 4. Launch Presto server

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ cd presto-server-0.212
```
</div>

To run in foreground mode.
<div class='copy separator-dollar'>
```sh
$ ./bin/launcher run       
```
</div>

To run in background mode.
<div class='copy separator-dollar'>
```sh
$ ./bin/launcher start  
```
</div>

## 5. Test Presto queries

Use the presto CLI to run ad-hoc queries. 
<div class='copy separator-dollar'>
```sh
$ ./bin/presto --server localhost:8080 --catalog cassandra --schema default
```
</div>

Start using `myapp`.
<div class='copy separator-gt'>
```sql
presto:default> use myapp;
```
</div>
```sh
USE
```

Show the tables available.
<div class='copy separator-gt'>
```sql
presto:myapp> show tables;
```
</div>
```
 Table
-------
 stock_market
(1 row)
```

Describe a particular table.
<div class='copy separator-gt'>
```sql
presto:myapp> describe stock_market;
```
</div>
```sh
    Column     |  Type   | Extra | Comment 
---------------+---------+-------+---------
 stock_symbol  | varchar |       |         
 ts            | varchar |       |         
 current_price | real    |       |         
(3 rows)
```

### Query with filter

You can do this as shown below.
<div class='copy separator-gt'>
```sql
presto:myapp> select * from stock_market where stock_symbol = 'AAPL';
```
</div>
```sh
 stock_symbol |         ts          | current_price 
--------------+---------------------+---------------
 AAPL         | 2017-10-26 09:00:00 |        157.41 
 AAPL         | 2017-10-26 10:00:00 |         157.0 
(2 rows)
```

### Query with aggregates

You can do this as shown below.
<div class='copy separator-gt'>
```sql
presto:myapp> select stock_symbol, avg(current_price) from stock_market group by stock_symbol;
```
</div>
```sh
 stock_symbol |  _col1  
--------------+---------
 GOOG         | 972.235 
 AAPL         | 157.205 
 FB           | 170.365 
(3 rows)
```
