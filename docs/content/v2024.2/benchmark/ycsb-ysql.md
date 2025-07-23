---
title: Benchmark YSQL performance using YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Learn how to test the YSQL api using the new YSQL binding.
headcontent: Benchmark YSQL performance using YCSB
menu:
  v2024.2:
    identifier: ycsb-2-ysql
    parent: benchmark
    weight: 5
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ycsb-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      JDBC Binding
    </a>
  </li>

  <li >
    <a href="../ycsb-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL Binding
    </a>
  </li>

  <li >
    <a href="../ycsb-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL Binding
    </a>
  </li>

</ul>

This document describes how to use a YSQL-specific binding to test the YSQL API using the YCSB benchmark.

For additional information about YCSB, refer to the following:

* [YCSB Wiki](https://github.com/brianfrankcooper/YCSB/wiki)
* [Workload info](https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads)

## Running the benchmark

To run the benchmark, ensure that you meet the prerequisites and complete steps such as starting YugabyteDB and configuring its properties.

### Prerequisites

The binaries are compiled with Java 13 and it is recommended to run these binaries with that version.

Run the following commands to download the YCSB binaries:

```sh
$ cd $HOME
$ wget https://github.com/yugabyte/YCSB/releases/download/1.0/ycsb.tar.gz
$ tar -zxvf ycsb.tar.gz
$ cd YCSB
```

Ensure that you have the YSQL shell [ysqlsh](../../api/ysqlsh/) and that its location is included in the `PATH` variable, as follows:

```sh
$ export PATH=$PATH:/path/to/ysqlsh
```

### Start YugabyteDB

Start your YugabyteDB cluster by following the procedure described in [Manual deployment](../../deploy/manual-deployment/). Note the IP addresses of the nodes in the cluster, as these addresses are required when configuring the properties file.

### Configure the properties file

Update the file `db.properties` in the YCSB directory with the following contents, replacing values for the IP addresses in the `db.url` field with the correct values for all the nodes that are part of the cluster:

```sh
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://<ip1>:5433/ycsb;jdbc:postgresql://<ip2>:5433/ycsb;jdbc:postgresql://<ip3>:5433/ycsb;
db.user=yugabyte
db.passwd=
```

The other configuration parameters are described in [Core Properties](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties).

### Run the benchmark

Use the following script `run_ysql.sh` to load and run all the workloads:

```sh
$ ./run_ysql.sh --ip <ip>
```

The preceding command runs the workload on a table with a million rows. To run the benchmark on a table with a different row count, use the following command:

```sh
$ ./run_ysql.sh --ip <ip> --recordcount <number of rows>
```

To get the maximum performance out of the system, you would have to tune the `threadcount` parameter in the script. As a reference, for a c5.4xlarge instance with 16 cores and 32GB RAM, you used a `thread count` of 32 for the loading phase and 256 for the execution phase.

### Verify results

The `run_ysql.sh` script creates two result files per workload: one for the loading, and one for the execution phase with the details of throughput and latency.

For example, for a workload it creates, inspect the `workloada-ysql-load.dat` and `workloada-ysql-transaction.dat` files.

### Run individual workloads (optional)

Optionally, you can run workloads individually using the following steps:

1. Start the YSQL shell using the following command:

    ```sh
    $ ./bin/ysqlsh -h <ip>
    ```

1. Create the `ycsb` database as follows:

    ```sql
    yugabyte=# CREATE DATABASE ycsb;
    ```

1. Connect to the database as follows:

    ```sql
    yugabyte=# \c ycsb
    ```

1. Create the table as follows:

    ```sql
    ycsb=# CREATE TABLE usertable (
               YCSB_KEY VARCHAR(255) PRIMARY KEY,
               FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT,
               FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT,
               FIELD8 TEXT, FIELD9 TEXT);
    ```

1. Load the data before you start the `yugabyteSQL` workload:

    ```sh
    $ ./bin/ycsb load yugabyteSQL -s \
          -P db.properties           \
          -P workloads/workloada     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=32
    ```

1. Run the workload as follows:

    {{< note title="Note" >}}
The `recordcount` parameter in the following `ycsb` commands should match the number of rows in the table.
    {{< /note >}}

    ```sh
    $ ./bin/ycsb run yugabyteSQL -s  \
          -P db.properties           \
          -P workloads/workloada     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

1. Run other workloads (for example, `workloadb`) by changing the corresponding argument in the preceding command, as follows:

    ```sh
    $ ./bin/ycsb run yugabyteSQL -s  \
          -P db.properties           \
          -P workloads/workloadb     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

## Expected results

When run on a 3-node cluster with each node on a c5.4xlarge AWS instance (16 cores, 32 GB of RAM, and 2 EBS volumes), all belonging to the same availability zone with the client VM running in the same availability zone, you get the following results for _1 million rows_:

| Workload | Throughput (ops/sec) | Read Latency | Write Latency |
| :------- | :------------------- | :------------| :------------ |
| Workload A | 37,377 | 1.5ms | 12 ms update |
| Workload B | 66,875 | 4ms | 7.6ms update |
| Workload C | 77,068 | 3.5ms read | Not applicable |
| Workload D | 63,676 | 4ms | 7ms insert |
| Workload E | 63,686 | 3.8ms scan | Not applicable |
| Workload F | 29,500 | 2ms | 15ms read-modify-write |
