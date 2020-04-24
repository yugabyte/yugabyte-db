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

{{< note title="Note" >}}

This is a fork of the sysbench utility with a few additions to better reflect
the database's distributed nature.

{{< /note >}}

## Step 1. Get Sysbench.

You can do this by running the following commands.

```sh
$ cd $HOME
$ git clone https://github.com/yugabyte/sysbench.git
$ cd sysbench
$ ./autogen.sh && ./configure --with-pgsql && make -j && sudo make install
```

{{< note title="Note" >}}
The above steps installs the sysbench utility in '/usr/local/bin'
{{< /note >}}

## Step 2. Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Quick start](https://docs.yugabyte.com/latest/quick-start/explore-ysql/).

## Step 3. Run the benchmark

There is a handly shell script 'run_sysbench.sh' that loads the data and runs the various workloads.
```sh
./run_sysbench.sh
```

## Manually run the workloads

Before starting the workload we need to load the data first.

```sh
sysbench oltp_read_only --table-size=1000000 --range_key_partitioning=true --serial_cache_size=1000 --db-driver=pgsql --pgsql-host=127.0.0.1 --pgsql-port=5433 --pgsql-user=yugabyte --pgsql-db=yugabyte prepare
```

Then we can run the workload as follows

```sh
sysbench oltp_read_only --table-size=1000000 --range_key_partitioning=true --db-driver=pgsql --pgsql-host=127.0.0.1 --pgsql-port=5433 --pgsql-user=yugabyte --pgsql-db=yugabyte --threads=64 --time=120 --warmup-time=120 run
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
