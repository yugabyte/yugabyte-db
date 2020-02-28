---
title: TPC-C
linkTitle: TPC-C
description: TPC-C
headcontent: Benchmark YugabyteDB using TPC-C
menu:
  latest:
    identifier: tpcc-ysql
    parent: benchmark
    weight: 4
aliases:
  - /benchmark/tpcc/
showAsideToc: true
isTocNested: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/benchmark/tpcc-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

Follow the steps below to run the open-source [oltpbench](https://github.com/oltpbenchmark/oltpbench) TPC-C workload against YugabyteDB YSQL. [TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity that are either either executed online or queued for deferred execution.

## Step 1. Download the TPC-C binaries

To download the TPC-C binaries, run the following commands.

```sh
$ cd $HOME
$ wget https://github.com/yugabyte/tpcc/releases/download/1.0/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

## Step 2. Start your database

Start the database using the steps mentioned [here](https://docs.yugabyte.com/latest/quick-start/create-local-cluster/macos/).

## Step 3. Configure connection properties

Set the following connection configurations in the workload configuration file (`config/workload_1.xml`).

```sh
<!-- Connection details -->
<dbtype>postgres</dbtype>
<driver>org.postgresql.Driver</driver>
<DBUrl>jdbc:postgresql://<ip>:5433/yugabyte</DBUrl>
<username>yugabyte</username>
<password></password>
<isolation>TRANSACTION_REPEATABLE_READ</isolation>
```

The details of the workloads have already been populated in the sample configuration files located in the `/config` directory.
The workload descriptor works the same way as it does in the upstream branch and details can be found in the [online documentation](https://github.com/oltpbenchmark/oltpbench/wiki).

## Step 4. Running the TPC-C benchmark

Use the provided utility script (`./tpccbenchmark`) to run the TPC-C benchmark. Available options are:

```
-c,--config &lt;arg&gt;            [required] Workload configuration file
   --clear &lt;arg&gt;             Clear all records in the database for this benchmark
   --create &lt;arg&gt;            Initialize the database for this benchmark
   --dialects-export &lt;arg&gt;   Export benchmark SQL to a dialects file
   --execute &lt;arg&gt;           Execute the benchmark workload
-h,--help                          Print this help
   --histograms                    Print txn histograms
   --load &lt;arg&gt;              Load data using the benchmark's data loader
-o,--output &lt;arg&gt;            Output file (default System.out)
   --runscript &lt;arg&gt;         Run an SQL script
-s,--sample &lt;arg&gt;            Sampling window
-v,--verbose                       Display Messages
```

Before starting the workload, you will need to load the data first.

```sh
$ ./tpccbenchmark -c config/workload_1.xml --create=true --load=true
```

Then, you can run the workload.

```sh
$ ./tpccbenchmark -c config/workload_1.xml --execute=true -s 5 -o outputfile
```

You can also load and run the benchmark in a single step:

```sh
$ ./tpccbenchmark -c config/workload_1.xml --create=true --load=true --execute=true -s 5 -o outputfile
```

The `config` directory has different configurations for various workloads. You can run any of those workloads by changing the configuration file.

```sh
$ ./tpccbenchmark -c config/workload_2.xml --create=true --load=true --execute=true -s 5 -o outputfile
```

## Output

The raw output is a listing of start time (in Java microseconds) and duration (microseconds) for each transaction type.

```
transaction type (index in config file), start time (microseconds),latency (microseconds)
3,1323686190.045091,8677
4,1323686190.050116,6800
4,1323686190.055146,3221
3,1323686190.060193,1459
4,1323686190.065246,2476
4,1323686190.070348,1834
4,1323686190.075342,1904
```

To obtain transactions per second (TPS), you can aggregate the results into windows using the `-s 1` option. The throughput and different latency measures in milliseconds are reported.

```
time (seconds),throughput (requests/s),average,min,25th,median,75th,90th,95th,99th,max
0,200.200,1.183,0.585,0.945,1.090,1.266,1.516,1.715,2.316,12.656
5,199.800,0.994,0.575,0.831,0.964,1.071,1.209,1.424,2.223,2.657
10,200.000,0.984,0.550,0.796,0.909,1.029,1.191,1.357,2.024,35.835
```
