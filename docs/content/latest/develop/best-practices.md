---
title: Best practices
linkTitle: Best practices
description: Best practices when using YugabyteDB
aliases:
  - /latest/quick-start/best-practices-dev/
menu:
  latest:
    identifier: best-practices-dev
    parent: develop
    weight: 582
isTocNested: 4
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="" class="nav-link active">
      <i class="icon-" aria-hidden="true"></i>
      General
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ysql.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>


Developing applications using distributed databases requires more thought about 
schemas and queries to keep a well balancer cluster, low query latency and preventing 
data hotspots. Below is a list of best of practices that you can keep in mind
while using YugabyteDB using all layers.

## YSQL compared to YCQL

The first step is which API to use. For an in depth comparison see below:

- The YCQL API doesn't support client-controlled multi-step transactions where you can issue multiple DML/SELECT requests across separate calls, and then finally choose to commit or rollback. This more general version of multi-row/multi-table transactions is only available in the YSQL API of YugabyteDB.
- YCQL does support multi-row transactions in the more limited form (usually called `autocommit`) where the entire block from `BEGIN TRANS .. END TRANS` is a single compound statement that comes to the server in one shot. 
It is not as flexible as a `BEGIN... END` block where you can mix control-flow (e.g. if/then/else) or have local variables for things you are SELECTing in between etc. 
It only supports DML statement, not SELECTs.
- YCQL doesn't at the moment support range sharding.
- YCQL has cluster aware drivers meaning they can discover all nodes of the cluster, get notified of node add/remove and therefore do not need a loadbalancer.
The client drivers are also topology aware, and can perform reads from nearest region/datacenter.
The drivers are also shard aware, they know the location of tablet leaders in the cluster and can query them directly.
YSQL clients aren't yet cluster aware and need to use loadbalancers or custom code to keep track of the cluster state.
We're working on cluster aware drivers starting with [JDBC](https://github.com/yugabyte/jdbc-yugabytedb)  
- YCQL supports automatic expiry of data using the TTL feature - you can set a retention policy for data at table/row/column level and the older data is automatically purged from the DB.
- YCQL supports collection data types such as sets, maps, lists. Note that both YCQL and YSQL support JSONB which can be used to model collections.
- YCQL uses threads to handle client queries, while the YSQL PostgreSQL code uses processes. 
These processes are heavier weight, so this cuts throughput and affects connection scalability. 
- In YCQL, each insert is treated as an upsert (update or insert) by default and needs special syntax to perform pure inserts. 
In YSQL, each insert needs to read the data before performing the insert since if the key already exists, it is treated as a failure.
- YCQL is Cassandra API compatible, and therefore supports the Cassandra ecosystem (like Spark and Kafka connectors, JanusGraph and KairosDB compatibility, etc). 
Note that these ecosystem integrations can be built on top of YSQL easily.
- YSQL is built on top of Postgresql and has a lot more features like foreign keys, stored procedures, SERIAL types, 
CTE queries, joins, subqueries, extensions, etc.

Currently, we have focused only on correctness + functionality for YSQL and are just getting started with performance, 
while YCQL performance has been worked on quite a bit. Over time YSQL performance will be on parity with YCQL.


## Primary key and index sizing
Yugabyte tables require a primary key and rows are stored on disk ordered by the primary key columns.
This means rows are clustered on disk by their primary key columns.
Rows are compressed into blocks and a block index is used to find the right block
when querying the db. 

You have to be careful regarding the size of the primary keys since they will
also be included in every secondary index of the table and will make the block-index larger.
     

## Hardware sizing
See [hardware sizing](/latest/deploy/checklist/) docs.


## Tablet leaders in multi-az and multi-region deployments
In multi-region deployments, we can hint the database to try and keep all tablet-leaders
in 1 region thus lowering the latency and network hops during read/write transactions.
That can be done using the `set_preferred_zones` subcommand of [yb-admin](../../admin/yb-admin) cli.

## Settings for ci/cd/integration-tests:
Using YugabyteDB in (ci,cd,automated tests) scenarios we can set certain gflags 
to lower durability and increase performance.

Tserver gflags:

1. `--fs_data_dirs` to temp/memory directory
2. `--yb_num_shards_per_tserver=1`
3. `--durable_wal_write=false`
4. `--interval_durable_wal_write_ms=10000`
5. `--bytes_durable_wal_write_mb=25`

Master gflags:

1. `--yb_num_shards_per_tserver=1`
2. `--replication_factor=1`

## Single AZ deployments
In single AZ deployments, you need to set `--durable_wal_write=true` [gflag](../../reference/configuration/yb-tserver) in 
tserver to not lose data if the whole datacenter goes down (power failure etc).

## Use SSD instead of Hard Disk Drives (HDD)
Currently HDD aren't supported by YugabyteDB. One of the reasons is that each tablet has it's own 
WAL (write ahead log) and even though all writes are sequential, having multiple WALs, even though each WAL is written sequentially, 
the writes may end up as random-writes when multiple WAL are flushing at the same time. 
We're working hard on moving on per-server WAL 
to reduce overhead and making HDD a possible alternative. You can track 
[1K+ tablets issue](https://github.com/yugabyte/yugabyte-db/issues/1317).


Reach out on slack/forum for help with your data-schemas and how to better integrate best practices in your project.
