---
title: Benchmark YCQL performance using YCSB
headerTitle: YCSB
linkTitle: YCSB
description: Benchmark YCQL performance with YCSB using the new YCQL binding.
headcontent: Benchmark YCQL performance using YCSB.
menu:
  v2.12:
    identifier: ycsb-3-ycql
    parent: benchmark
    weight: 5
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ycsb-jdbc.md" >}}" class="nav-link">
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
    <a href="{{< relref "./ycsb-ycql.md" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL Binding
    </a>
  </li>

</ul>

This document describes how to use a YCQL-specific binding to test the YCQL API using the YCSB benchmark.

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

Ensure that you have the YCQL shell [ycqlsh](../../admin/ycqlsh/) and that its location is included in the `PATH` variable, as follows:

```sh
$ export PATH=$PATH:/path/to/ycqlsh
```

### Start YugabyteDB

Start your YugabyteDB cluster by following the procedure described in [Manual deployment](../../deploy/manual-deployment/). Note the IP addresses of the nodes in the cluster, as these addresses are required when configuring the properties file.

### Configure the properties file

Update the file `db.properties` in the YCSB directory with the following contents, replacing values for the IP addresses in the `hosts` field with the correct values for all the nodes that are part of the cluster:

```sh
hosts=<ip>
port=9042
cassandra.username=yugabyte
```

The other configuration parameters are described in [Core Properties](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties).

### Run the benchmark

Use the following script `run_ycql.sh` to load and run all the workloads:

```sh
$ ./run_ycql.sh --ip <ip>
```

The preceding command runs the workload on a table with a million rows. To run the benchmark on a table with a different row count, use the following command:

```sh
$ ./run_ycql.sh --ip <ip> --recordcount <number of rows>
```

To get the maximum performance out of the system, you would have to tune the `threadcount` parameter in the script. As a reference, for a c5.4xlarge instance with 16 cores and 32 GB RAM, we used a `threadcount` of `32` for the loading phase and `256` for the execution phase.

### Verify results

The `run_ycql.sh` script creates two result files per workload: one for the loading, and one for the execution phase with the details of throughput and latency.

For example, for a workload it creates, inspect the `workloada-ycql-load.dat` and `workloada-ycql-transaction.dat` files.

### Run individual workloads (optional)

Optionally, you can run workloads individually using the following steps:

1. Start the YCQL shell using the following command:

    ```sh
    $ ./bin/ycqlsh <ip>
    ```

1. Create the `ycsb` keyspace as follows:

    ```cql
    ycqlsh> CREATE KEYSPACE ycsb;
    ```

1. Connect to the keyspace as follows:

    ```cql
    ycqlsh> USE ycsb;
    ```

1. Create the table as follows:

    ```cql
    ycqlsh:ycsb> create table usertable (
                    y_id varchar primary key,
                    field0 varchar, field1 varchar, field2 varchar, field3 varchar,
                    field4 varchar, field5 varchar, field6 varchar, field7 varchar,
                    field8 varchar, field9 varchar);
    ```

1. Load the data before you start the `yugabyteCQL` workload:

    ```sh
    $ ./bin/ycsb load yugabyteCQL -s \
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
    $ ./bin/ycsb run yugabyteCQL -s  \
          -P db.properties           \
          -P workloads/workloada     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

1. Run other workloads (for example, `workloadb`) by changing the corresponding argument in the preceding command, as follows:

    ```sh
    $ ./bin/ycsb run yugabyteCQL -s  \
          -P db.properties           \
          -P workloads/workloadb     \
          -p recordcount=1000000     \
          -p operationcount=10000000 \
          -p threadcount=256
    ```

## Expected results

When run on a 3-node cluster with each a `c5.4xlarge` AWS instance (16 cores, 32 GB of RAM, and 2 EBS volumes) all belonging to the same availability zone with the client VM running in the same availability zone, you get the following results for _1 million rows_:

| Workload | Throughput (ops/sec) | Read Latency | Write Latency |
| :------- | -------------------: | :----------- | :------------ |
| Workload A | 108,249 | 1 ms | 3.5 ms update |
| Workload B | 141,061 | 1.6 ms | 4 ms update |
| Workload C | 188,111 | 1.3 ms | Not applicable |
| Workload D | 153,165 | 1.5 ms | 4.5 ms insert |
| Workload E | 23,489 | 10 ms scan | Not applicable |
| Workload F | 80,451 | 1 ms | 5 ms read-modify-write |
