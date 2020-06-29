---
title: Benchmark YSQL performance using TPC-C
headerTitle: TPC-C
linkTitle: TPC-C
description: Benchmark YSQL performance using TPC-C
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

## Overview
Follow the steps below to run the open-source [oltpbench](https://github.com/oltpbenchmark/oltpbench) TPC-C workload against YugabyteDB YSQL. [TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity that are either either executed online or queued for deferred execution.

## Running the benchmark

### 1. Prerequisites

To download the TPC-C binaries, run the following commands.

```sh
$ cd $HOME
$ wget https://github.com/yugabyte/tpcc/releases/download/1.1/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

{{< note title="Note" >}}
The binaries are compiled with JAVA 13 and it is recommended to run these binaries with that version.
{{< /note >}}

Start your YugabyteDB cluster by following the steps [here](../../deploy/manual-deployment/).

{{< tip title="Tip" >}}
You will need the IP addresses of the nodes in the cluster for the next step.
{{< /tip>}}


### Step 2. TPC-C Load Phase

Before starting the workload, you will need to load the data first. Make sure
to replace the IP addresses with that of the nodes in the cluster.


```sh
$ ./tpccbenchmark --create=true --load=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100 \
  --loaderthreads=48
```

{{< note title="Note" >}}
The default values for `nodes` is `127.0.0.1`, `warehouses` is `10` and
`loaderthreads` is `10`.
{{< /note >}}

### Step 3. TPC-C Execute Phase

You can then run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100  \
  -o outputfile
```

{{< tip title="Tip" >}}
You can also load and run the benchmark in a single step:
```sh
$ ./tpccbenchmark --create=true --load=true --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100 \
  -o outputfile
```
{{< /tip>}}

## TPC-C Benchmark Results

Once the execution is done the TPM-C number along with the efficiency is printed.

```
01:25:34,669 (DBWorkload.java:880) INFO  - Rate limited reqs/s: Results(nanoSeconds=1800000482491, measuredRequests=423634) = 235.35215913594521 requests/sec
01:25:34,669 (DBWorkload.java:885) INFO  - Num New Order transactions : 186826, time seconds: 1800
01:25:34,669 (DBWorkload.java:886) INFO  - TPM-C: 6227
01:25:34,669 (DBWorkload.java:887) INFO  - Efficiency : 96.84292379471229
01:25:34,671 (DBWorkload.java:722) INFO  - Output Raw data into file: results/outputfile.csv
```

## Configure the benchmark

We can change the default username, password, port, etc. using the configuration file at `config/workload_all.xml`. We can also change the terminals or the physical connections being used by the benchmark using the configuration.
```sh
<dbtype>postgres</dbtype>
<driver>org.postgresql.Driver</driver>
<port>5433</5433>
<username>yugabyte</username>
<password></password>
<isolation>TRANSACTION_REPEATABLE_READ</isolation>

<terminals>100</terminals>
<numDBConnections>10</numDBConnections>
```

{{< note title="Note" >}}
By default the number of terminals is 10 times the number of warehouses which is the max that the TPC-C spec allows. The number of DB connections is the same as the number of warehouses.
{{< /note >}}
