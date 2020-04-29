---
title: Benchmark YCQL performance using YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Benchmark YugabyteDB YCQL performance using YCSB.
aliases:
  - /latest/benchmark/ycsb
menu:
  latest:
    identifier: ycsb-2-ycql
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
    <a href="/latest/benchmark/ycsb-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL-JDBC
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

{{< note title="Note" >}}

The binaries are compiled with JAVA 13 and it is recommended to run these binaries with that version.

{{< /note >}}

## Step 2. Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Quick start](https://docs.yugabyte.com/latest/quick-start/explore-ysql/).

## Step 3. Configure YCSB connection properties

Set the following connection configuration options in `db.properties`:

```sh
hosts=<ip>
port=9042
cassandra.username=yugabyte
```

For details on other configuration parameters, like username, password, connection parameters, etc., see [YugabyteCQL binding](https://github.com/yugabyte/YCSB/tree/master/yugabyteCQL).

## Step 4. Run the workloads

There is a handy script (`run_cql.sh`) that loads and runs all the workloads.
First we need to supply the paths to the ycsb binary and the cqlsh binary (which is distributed as part of the database package) along with the IP of the tserver node.
Then you simply run the workloads as:

```sh
$ ./run_cql.sh
```

The script creates two result files per workload, one for the loading and one for the execution phase with the details of throughput and latency.

{{< note title="Note" >}}

To get the maximum performance out of the system, you would have to tune the `threadcount` parameter in the script. As a reference, for a c5.4xlarge instance with 16 cores and 32 GB RAM, we used a `threadcount` of 32 for the loading phase and 256 for the execution phase.

{{< /note >}}

### Expected Results
When run on a 3 node cluster with each a c5.4xlarge AWS instance (16 cores, 32GB of RAM and 2 EBS volumes) all belonging to the same AZ with the client VM running in the same AZ we get the following results:

|            | Throughput (ops/sec) | Latency (ms)
-------------|-----------|----------|
WorkloadA | 108249 | 1ms read 3.5 ms update
WorkloadB | 141061 | 1.6ms read 4ms update
WorkloadC | 188111 | 1.3ms read
WorkloadD | 153165 | 1.5ms read 4.5ms insert
WorkloadE | 23489 | 10ms scan
WorkloadF | 80451 | 1ms read 5ms read-modify-write

## Manually run the workloads

Create the keyspace and table using the `cqlsh` tool.
The `cqlsh` tool is distributed as part of the database package.

```sh
$ ./bin/cqlsh <ip> --execute "create keyspace ycsb"
$ ./bin/cqlsh <ip> --keyspace ycsb --execute 'create table usertable (y_id varchar primary key, field0 varchar, field1 varchar, field2 varchar, field3 varchar, field4 varchar, field5 varchar, field6 varchar, field7 varchar, field8 varchar, field9 varchar);'
```

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
