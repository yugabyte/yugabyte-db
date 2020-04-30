---
title: Benchmark YSQL performance with YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Benchmark YSQL performance with YCSB using the PostgreSQL JDBC driver.
menu:
  latest:
    identifier: ycsb-2-ysql
    parent: benchmark
    weight: 5
aliases:
  - /benchmark/ycsb/
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
    <a href="/latest/benchmark/ycsb-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL-JDBC
    </a>
  </li>

  <li >
    <a href="/latest/benchmark/ycsb-ycql/" class="nav-link">
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

## 1. Download the YCSB binaries

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

## 2. Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Quick start](https://docs.yugabyte.com/latest/quick-start/explore-ysql/).

## 3. Configure YCSB connection properties

Set the following connection configurations in `db.properties`:

```sh
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://<ip1>:5433/ycsb;jdbc:postgresql://<ip2>:5433/ycsb;jdbc:postgresql://<ip3>:5433/ycsb;
db.user=yugabyte
db.passwd=
```

The other configuration parameters, are described in detail at [this page](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties)

{{< note title="Note" >}}
The db.url field should be populated with the IPs of all the tserver nodes that are part of the cluster.
{{< /note >}}

## 4. Run individual workloads

Create the database and table using the `ysqlsh` tool.
The `ysqlsh` tool is distributed as part of the database package.

```sh
$ ./bin/ysqlsh -h <ip> -c 'create database ycsb;'
$ ./bin/ysqlsh -h <ip> -d ycsb -c 'CREATE TABLE usertable (YCSB_KEY VARCHAR(255) PRIMARY KEY, FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT, FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT, FIELD8 TEXT, FIELD9 TEXT);'
```

Before starting the `yugabyteSQL` workload, you will need to load the data first.

```sh
$ ./bin/ycsb load jdbc -P yugabyteSQL/db.properties -P workloads/workloada
```

Then, you can run the workload:

```sh
$ ./bin/ycsb run jdbc -P yugabyteSQL/db.properties -P workloads/workloada
```

To run the other workloads (for example, `workloadb`), all you need to do is change that argument in the above command.

```sh
$ ./bin/ycsb run jdbc -P yugabyteSQL/db.properties -P workloads/workloadb
```

## 5. Expected results

When run on a 3-node cluster with each a c5.4xlarge AWS instance (16 cores, 32GB of RAM and 2 EBS volumes) all belonging to the same AZ with the client VM running in the same AZ we get the following results:

| Workload           | Throughput (ops/sec) | Latency (ms)
-------------|-----------|----------|
WorkloadA | 37377 | 1.5ms read, 12 ms update
WorkloadB | 60801 | 4ms read, 7.6ms update
WorkloadC | 72152 | 3.5ms read
WorkloadD | 61226 | 4ms read, 7ms insert
WorkloadF | 29759 | 2ms read, 15ms read-modify-write
