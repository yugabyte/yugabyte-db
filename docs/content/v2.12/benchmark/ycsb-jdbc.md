---
title: Benchmark YSQL performance with YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Benchmark YSQL performance with YCSB using the standard JDBC binding.
headcontent: Benchmark YSQL performance with YCSB using the standard JDBC binding.
menu:
  v2.12:
    identifier: ycsb-1-ysql
    parent: benchmark
    weight: 5
type: docs
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
$ tar -xzf ycsb.tar.gz
$ cd YCSB
```

Ensure that you have the YSQL shell [ysqlsh](../../admin/ysqlsh/) and that its location is included in the `PATH` variable, as follows:

```sh
$ export PATH=$PATH:/path/to/ysqlsh
```

You can find `ysqlsh` in your YugabyteDB installation's `bin` directory. For example:

```sh
$ export PATH=$PATH:/Users/yugabyte/code/bin
```

### Start YugabyteDB

Start your YugabyteDB cluster by following the procedure described in [Manual deployment](../../deploy/manual-deployment/). Note the IP addresses of the nodes in the cluster, as these addresses are required when configuring the properties file.

### Configure the properties file

Update the file `db.properties` in the YCSB directory with the following contents, replacing values for the IP addresses in the `db.url` field with the correct values for all the nodes that are part of the cluster:

```properties
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://<ip1>:5433/ycsb;jdbc:postgresql://<ip2>:5433/ycsb;jdbc:postgresql://<ip3>:5433/ycsb;
db.user=yugabyte
db.passwd=
```

The other configuration parameters are described in [Core Properties](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties).

### Run the benchmark

Use the following script `run_jdbc.sh` to load and run all the workloads:

```sh
$ ./run_jdbc.sh --ip <ip>
```

The preceding command runs the workload on a table with a million rows. To run the benchmark on a table with a different row count, use the following command:

```sh
$ ./run_jdbc.sh --ip <ip> --recordcount <number of rows>
```

To obtain the maximum performance out of the system, you can tune the `threadcount` parameter in the script. As a reference, for a `c5.4xlarge` instance with 16 cores and 32GB RAM, you use a threadcount of 32 for the loading phase and 256 for the execution phase.

### Verify results

The `run_jdbc.sh` script creates two result files per workload: one for the loading, and one for the execution phase with the details of throughput and latency.

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

1. Connect to the database** as follows:

    ```sql
    yugabyte=# \c ycsb
    ```

1. Create the table as follows:

    ```sql
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

1. Load the data before you start the `jdbc` workload:

    ```sh
    $ ./bin/ycsb load jdbc -s        \
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
    $ ./bin/ycsb run jdbc -s         \
          -P db.properties           \
          -P workloads/workloada     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

1. Run other workloads (for example, `workloadb`) by changing the corresponding argument in the preceding command, as follows:

    ```sh
    $ ./bin/ycsb run jdbc -s         \
          -P db.properties           \
          -P workloads/workloadb     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

## Expected results

When run on a 3-node cluster of `c5.4xlarge` AWS instances (16 cores, 32GB of RAM, and 2 EBS volumes) all belonging to the same availability zone with the client VM running in the same availability zone, expect the following results for _1 million rows_:

| Workload | Throughput (ops/sec) | Read Latency | Write Latency |
| :------- | :------------------- | :----------- | :------------ |
| Workload A | 37,443 | 1.5ms | 12 ms update |
| Workload B | 66,875 | 4ms | 7.6ms update |
| Workload C | 77,068 | 3.5ms | Not applicable |
| Workload D | 63,676 | 4ms | 7ms insert |
| Workload E | 16,642 | 15ms scan | Not applicable |
| Workload F | 29,500 | 2ms | 15ms read-modify-write |

For an additional example, refer to [Example: YCSB workload with automatic tablet splitting example](../../architecture/docdb-sharding/tablet-splitting/#example-ycsb-workload-with-automatic-tablet-splitting).
