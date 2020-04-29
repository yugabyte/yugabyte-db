---
title: Benchmark YSQL performance using Sysbench
headerTitle: Sysbench
linkTitle: Sysbench
description: Benchmark YugabyteDB YSQL performance using Sysbench.
headcontent: Benchmark YugabyteDB YSQL performance using Sysbench.
menu:
  latest:
    identifier: sysbench-ysql
    parent: benchmark
    weight: 5
aliases:
  - /benchmark/sysbench/
showAsideToc: true
isTocNested: true
---
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/benchmark/sysbench-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

Sysbench is a popular tool for benchmarking databases like Postgres and MySQL, as well as system capabilities like CPU, memory and I/O.
Follow the steps below to run Sysbench against YugabyteDB.
The version we are using is forked from the [official](https://github.com/akopytov/sysbench) version with a few modifications to better reflect YugabyteDB's distributed nature.

## Step 1. Install Sysbench

You can do this by running the following commands.

```sh
$ cd $HOME
$ git clone https://github.com/yugabyte/sysbench.git
$ cd sysbench
$ ./autogen.sh && ./configure --with-pgsql && make -j && sudo make install
```

{{< note title="Note" >}}
The above steps will install the sysbench utility in '/usr/local/bin'
{{< /note >}}

## Step 2. Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Quick start](https://docs.yugabyte.com/latest/quick-start/explore-ysql/).

## Step 3. Run all OLTP workloads

There is a handy shell script `run_sysbench.sh` that loads the data and runs the various workloads.
```sh
./run_sysbench.sh <ip>
```
This script runs all the 8 workloads using 64 threads with the number of tables as 10 and the table size as 100k.

{{< note title="Note" >}}
Make sure that the `ysqlsh` path is correctly configured in the shell script.
{{< /note >}}

### Expected Results
When run on a 3 node cluster with each a c5.4xlarge AWS instance (16 cores and 32GB of RAM) all belonging to the same AZ with the client VM running in the same AZ we get the following results:

|            | Throughput (txns/sec) | Latency (ms)
-------------|-----------|----------|
OLTP_READ_ONLY | 3276 | 39
OLTP_READ_WRITE | 487 | 265
OLTP_WRITE_ONLY | 1818 | 70
OLTP_POINT_SELECT| 95695 | 1.3
OLTP_INSERT | 6348 | 20.1
OLTP_UPDATE_INDEX | 4052 | 31
OLTP_UPDATE_NON_INDEX | 11496 | 11
OLTP_DELETE | 67499 | 1.9


### Manually run the workloads

Before starting the workload we need to load the data first.

```sh
sysbench oltp_point_select --table-size=1000000 --range_key_partitioning=true --serial_cache_size=1000 --db-driver=pgsql --pgsql-host=127.0.0.1 --pgsql-port=5433 --pgsql-user=yugabyte --pgsql-db=yugabyte prepare
```

Then we can run the workload as follows

```sh
sysbench oltp_point_select --table-size=1000000 --range_key_partitioning=true --db-driver=pgsql --pgsql-host=127.0.0.1 --pgsql-port=5433 --pgsql-user=yugabyte --pgsql-db=yugabyte --threads=64 --time=120 --warmup-time=120 run
```

The choice of different workloads are:
* oltp_insert
* oltp_point_select
* oltp_write_only
* oltp_read_only
* oltp_read_write
* oltp_update_index
* oltp_update_non_index
* oltp_delete
