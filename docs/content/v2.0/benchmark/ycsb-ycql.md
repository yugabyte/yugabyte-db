---
title: YCSB
linkTitle: YCSB
description: YCSB
headcontent: Benchmark YugabyteDB using YCSB.
block_indexing: true
menu:
  v2.0:
    identifier: ycsb-1-ycql
    parent: benchmark
    weight: 5
showAsideToc: true
isTocNested: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/benchmark/ycsb-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="/latest/benchmark/ycsb-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

{{< note title="Note" >}}

For more information about YCSB, see: 

* YCSB Wiki: https://github.com/brianfrankcooper/YCSB/wiki
* Workload info: https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads

{{< /note >}}

## Step 1. Download the YCSB binaries

You can do this by running the following commands.

```sh
$ cd $HOME
$ wget https://github.com/yugabyte/YCSB/releases/download/1.0/ycsb.tar.gz
$ tar -zxvf ycsb.tar.gz
$ cd YCSB
```

## Step 2. Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Quick start](/latest/quick-start/explore-ysql/).

## Step 3. Create your keyspace

Create the keyspace and table using the `cqlsh` tool.
The `cqlsh` tool is distributed as part of the database package.

```sh
$ ./bin/cqlsh <ip> --execute "create keyspace ycsb"
$ ./bin/cqlsh <ip> --keyspace ycsb --execute 'create table usertable (y_id varchar primary key, field0 varchar, field1 varchar, field2 varchar, field3 varchar, field4 varchar, field5 varchar, field6 varchar, field7 varchar, field8 varchar, field9 varchar);'
```

## Step 4. Configure YCSB connection properties

Set the following connection configuration options in `db.properties`:

```sh
hosts=<ip>
port=9042
cassandra.username=yugabyte
```

For details on other configuration parameters, like username, password, connection
parameters, etc., see [YugabyteCQL binding](https://github.com/yugabyte/YCSB/tree/master/yugabyteCQL).

## Step 5. Running the workload

Before starting the `yugabyteCQL` workload, you first need to load the data.

```sh
$ ./bin/ycsb load yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloada
```

Then, you can run the workload:

```sh
$ ./bin/ycsb run yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloada
```

To run the other workloads (for example, `workloadb`), all we need to do is change that argument in the above command.

```sh
$ ./bin/ycsb run yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloadb
```
