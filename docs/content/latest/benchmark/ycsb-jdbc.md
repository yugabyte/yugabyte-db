---
title: Benchmark YSQL performance with YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Benchmark YSQL performance with YCSB using the standard JDBC binding.
menu:
  latest:
    identifier: ycsb-1-ysql
    parent: benchmark
    weight: 5
aliases:
  - /benchmark/ycsb/
showAsideToc: true
isTocNested: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ycsb-jdbc.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      JDBC Binding
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycsb-ysql.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL Binding
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycsb-ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL Binding
    </a>
  </li>

</ul>

This document describes how to use the standard JDBC binding to run the YCSB benchmark.

For additional information about YCSB, see the following: 

* [YCSB Wiki](https://github.com/brianfrankcooper/YCSB/wiki)
* [Workload info](https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads)

## Running the Benchmark

To run the benchmark, ensure that you meet the prerequisites and complete steps such as starting YugabyteDB and configuring its properties.

### Prerequisites

The binaries are compiled with JAVA 13 and it is recommended to run these binaries with that version.

Download the YCSB binariesby executing the following commands:

```sh
$ cd $HOME
$ wget https://github.com/yugabyte/YCSB/releases/download/1.0/ycsb.tar.gz
$ tar -zxvf ycsb.tar.gz
$ cd YCSB
```

Ensure that you have the YSQL shell `ysqlsh` and that it is exported to the `PATH` variable, as follows:
```sh
$ export PATH=$PATH:/path/to/ysqlsh
```

### Start YugabyteDB

Start your YugabyteDB cluster by following the procedure described in [Manual Deployment](../../deploy/manual-deployment/).
Take a note of the IP addresses of the nodes in the cluster, as these addresses are required when configuring the properties file.

### Configure the Properties File

Update the file `db.properties` in the YCSB directory with the following contents, replacing values for the IP addresses in the `db.url` field with the correct values for all the nodes that are part of the cluster:

```sh
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://<ip1>:5433/ycsb;jdbc:postgresql://<ip2>:5433/ycsb;jdbc:postgresql://<ip3>:5433/ycsb;
db.user=yugabyte
db.passwd=
```

The other configuration parameters are described in [Core Properties](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties).

### Run the Benchmark

Use the following script `run_jdbc.sh` to load and run all the workloads:

```sh
$ ./run_jdbc.sh --ip <ip>
```

The preceding command runs the workload on a table with a million rows. If you want to run the benchmark on a table with a different row count, use the following command:
```sh
$ ./run_jdbc.sh --ip <ip> --recordcount <number of rows>
```


To obtain the maximum performance out of the system, you can tune the `threadcount` parameter in the script. As a reference, for a c5.4xlarge instance with 16 cores and 32GB RAM, you use a threadcount of 32 for the loading phase and 256 for the execution phase.

### Verify Results

The `run_jdbc.sh` script creates two result files per workload: one for the loading and one for the execution phase with the details of throughput and latency.

For example for a workload it creates, see `workloada-ysql-load.dat` and `workloada-ysql-transaction.dat`

### Run Individual Workloads (optional)

Optionally, you can run individual workloads as follows:

- Connect to the database using by executing the following command:

  ```sh
  $ ./bin/ysqlsh -h <ip>
  ```

- Create the `ycsb` database as follows:

  ```postgres
  yugabyte=# CREATE DATABASE ycsb;
  ```

- Connect to the created database as follows:

  ```postgres
  yugabyte=# \c ycsb
  ```

- Create the table, as shown in the following example:

  ```postgres
  ycsb=# CREATE TABLE usertable (
             YCSB_KEY TEXT,
             FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT,
             FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT,
             FIELD8 TEXT, FIELD9 TEXT,
             PRIMARY KEY (YCSB_KEY ASC))
             SPLIT AT VALUES (('user10'),('user14'),('user18'),
             ('user22'),('user26'),('user30'),('user34'),('user38'),
             ('user42'),('user46'),('user50'),('user54'),('user58'),
             ('user62'),('user66'),('user70'),('user74'),('user78'),
             ('user82'),('user86'),('user90'),('user94'),('user98'));
  ```

- Before starting the `jdbc` workload, you load the data by executing the following command:

  ```sh
  $ ./bin/ycsb load jdbc -s        \
        -P db.properties           \
        -P workloads/workloada     \
        -p recordcount=1000000     \
        -p operationcount=10000000 \
        -p threadcount=32          \
        -p maxexecutiontime=180
  ```

- Run the workload by executing the following command:

  ```sh
  $ ./bin/ycsb run jdbc -s         \
        -P db.properties           \
        -P workloads/workloada     \
        -p recordcount=1000000     \
        -p operationcount=10000000 \
        -p threadcount=256         \
        -p maxexecutiontime=180
  ```

- To run the other workloads (for example, `workloadb`), change the corresponding argument in the preceding command, as follows:

  ```sh
  $ ./bin/ycsb run jdbc -s         \
        -P db.properties           \
        -P workloads/workloadb     \
        -p recordcount=1000000     \
        -p operationcount=10000000 \
        -p threadcount=256         \
        -p maxexecutiontime=180
  ```

### Expected Results

When run on a 3-node cluster with each a c5.4xlarge AWS instance (16 cores, 32GB of RAM and 2 EBS volumes) all belonging to the same AZ with the client VM running in the same AZ, expect to see the following results:

**One Million Rows**

| Workload | Throughput (ops/sec) | Read Latency | Write Latency
-------------|-----------|------------|------------|
Workload A | 37,443 | 1.5ms | 12 ms update
Workload B | 66,875 | 4ms | 7.6ms update
Workload C | 77,068 | 3.5ms | Not applicable
Workload D | 63,676 | 4ms | 7ms insert
Workload E | 16,642 | 15ms scan | Not applicable
Workload F | 29,500 | 2ms | 15ms read-modify-write

### Additional Examples

For additional examples, see [Example Using a YCSB Workload with Automatic Tablet Splitting](https://docs.yugabyte.com/latest/architecture/docdb-sharding/tablet-splitting/#example-using-a-ycsb-workload-with-automatic-tablet-splitting).

