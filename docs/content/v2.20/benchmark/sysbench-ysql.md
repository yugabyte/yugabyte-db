---
title: Benchmark YSQL performance using sysbench
headerTitle: sysbench
linkTitle: sysbench
description: Benchmark YSQL performance using sysbench.
headcontent: Benchmark YSQL performance using sysbench
menu:
  v2.20:
    identifier: sysbench-ysql
    parent: benchmark
    weight: 5
rightNav:
  hideH4: true
type: docs
---

sysbench is a popular tool for benchmarking databases like PostgreSQL and MySQL, as well as system capabilities like CPU, memory, and I/O. The [YugabyteDB version of sysbench](https://github.com/yugabyte/sysbench) is forked from the [official version](https://github.com/akopytov/sysbench) with a few modifications to better reflect YugabyteDB's distributed nature.

## Running the benchmark

### Prerequisites

To ensure the recommended hardware requirements are met and the database is correctly configured before benchmarking, review the [deployment checklist](../../deploy/checklist/). Make sure you have the [YSQL shell](../../api/ysqlsh/) `ysqlsh` exported to the `PATH` variable.

```sh
$ export PATH=$PATH:/path/to/ysqlsh
```

Install sysbench using the following steps for your operating system.

{{<tip>}}
For the latest packages please see [sysbench package](https://github.com/yugabyte/sysbench/releases/latest)
{{</tip>}}

#### Linux

```sh
wget https://github.com/yugabyte/sysbench/releases/download/1.0.0-yb/sysbench-1.0.0-1.el8.x86_64.rpm

sudo yum install -y sysbench-1.0.0-1.el8.x86_64.rpm
```

#### MacOS

```sh
brew install postgresql@14 wget

wget https://github.com/yugabyte/sysbench/releases/download/1.0.0-yb/Sysbench.pkg

sudo  installer -pkg Sysbench.pkg -target /
```

### Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Manual deployment](../../deploy/manual-deployment/).

{{< tip title="Tip" >}}
You will need the IP addresses of the nodes in the cluster for the next step.
{{< /tip>}}

### Run the benchmark

Run the `run_sysbench.sh` shell script to load the data and run the various workloads:

```sh
./run_sysbench.sh --ip <ip>
```

This script runs all 8 workloads using 64 threads with the number of tables as 10 and the table size as 100k.

If you want to run the benchmark with a different count of tables and tablesize, do the following:

```sh
./run_sysbench.sh --ip <ip> --numtables <number of tables> --tablesize <number of rows in each table>
```

### Run individual workloads (optional)

You can choose to run the following workloads individually:

* oltp_insert
* oltp_point_select
* oltp_write_only
* oltp_read_only
* oltp_read_write
* oltp_update_index
* oltp_update_non_index
* oltp_delete

Before starting the workload, load the data as follows:

```sh
$ sysbench <workload>               \
      --tables=10                   \
      --table-size=100000           \
      --range_key_partitioning=true \
      --db-driver=pgsql             \
      --pgsql-host=127.0.0.1        \
      --pgsql-port=5433             \
      --pgsql-user=yugabyte         \
      --pgsql-db=yugabyte           \
      prepare
```

Run a workload as follows:

```sh
$ sysbench <workload>               \
      --tables=10                   \
      --table-size=100000           \
      --range_key_partitioning=true \
      --db-driver=pgsql             \
      --pgsql-host=127.0.0.1        \
      --pgsql-port=5433             \
      --pgsql-user=yugabyte         \
      --pgsql-db=yugabyte           \
      --threads=64                  \
      --time=120                    \
      --warmup-time=120             \
      run
```

## Expected results

The following results are for a 3-node cluster with each node on a c5.4xlarge AWS instance (16 cores, 32 GB of RAM, and 2 EBS volumes), all in the same AZ with the client VM running in the same AZ.

### 10 tables each with 100k rows

|       Workload        | Throughput (txns/sec) | Latency (ms) |
| :-------------------- | :-------------------- | :----------- |
| OLTP_READ_ONLY        | 3276                  | 39           |
| OLTP_READ_WRITE       | 487                   | 265          |
| OLTP_WRITE_ONLY       | 1818                  | 70           |
| OLTP_POINT_SELECT     | 95695                 | 1.3          |
| OLTP_INSERT           | 6348                  | 20.1         |
| OLTP_UPDATE_INDEX     | 4052                  | 31           |
| OLTP_UPDATE_NON_INDEX | 11496                 | 11           |
| OLTP_DELETE           | 67499                 | 1.9          |
