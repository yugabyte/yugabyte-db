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
type: docs
---

sysbench is a popular tool for benchmarking databases like PostgreSQL and MySQL, as well as system capabilities like CPU, memory, and I/O. The [YugabyteDB version of sysbench](https://github.com/yugabyte/sysbench) is forked from the [official version](https://github.com/akopytov/sysbench) with a few modifications to better reflect YugabyteDB's distributed nature.

## Running the benchmark

### Prerequisites

To ensure the recommended hardware requirements are met and the database is correctly configured before benchmarking, review the [deployment checklist](../../deploy/checklist/).

Make sure you have the [YSQL shell](../../api/ysqlsh/) `ysqlsh` exported to the `PATH` variable.

```sh
$ export PATH=$PATH:/path/to/ysqlsh
```

### Install sysbench

Install sysbench on a machine which satisfies the Prerequisites using one of the following options:

<ul class="nav nav-tabs nav-tabs-yb">
    <li>
    <a href="#github" class="nav-link active" id="github-tab" data-bs-toggle="tab" role="tab" aria-controls="github" aria-selected="true">
      <i class="fab fa-github" aria-hidden="true"></i>
      Source
    </a>
  </li>
  <li>
    <a href="#rhel" class="nav-link" id="rhel-tab" data-bs-toggle="tab" role="tab" aria-controls="rhel" aria-selected="true">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      RHEL
    </a>
  </li>
  <li >
    <a href="#macos" class="nav-link" id="macos-tab" data-bs-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

</ul>

<div class="tab-content">
  <div id="github" class="tab-pane fade show active" role="tabpanel" aria-labelledby="github-tab">

Install sysbench using the following steps:

```sh
$ cd $HOME
$ git clone https://github.com/yugabyte/sysbench.git
$ cd sysbench
$ ./autogen.sh && ./configure --with-pgsql && make -j && sudo make install
```

  </div>

  <div id="rhel" class="tab-pane fade" role="tabpanel" aria-labelledby="rhel-tab">

{{< note title="Note" >}}

RHEL package is only for EL8

{{< /note >}}

```sh
wget https://github.com/yugabyte/sysbench/releases/download/1.0.0-yb/sysbench-1.0.0-1.el8.x86_64.rpm

sudo yum install -y sysbench-1.0.0-1.el8.x86_64.rpm 
```

  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">

{{< note title="Note" >}}

The MacOS package is only for Apple Silicon.

{{< /note >}}

```sh
brew install postgresql@14 wget

wget https://github.com/yugabyte/sysbench/releases/download/1.0.0-yb/Sysbench.pkg

sudo  installer -pkg Sysbench.pkg -target /
```

  </div>

</div>

This installs the sysbench utility in `/usr/local/bin`.

### Start YugabyteDB

Start your YugabyteDB cluster by following the steps in [Manual deployment](../../deploy/manual-deployment/).

{{< tip title="Tip" >}}
You will need the IP addresses of the nodes in the cluster for the next step.
{{< /tip>}}

### Run individual workloads

You can choose to run the following workloads individually:

* oltp_read_only
* oltp_read_write
* oltp_multi_insert
* oltp_update_index
* oltp_update_non_index
* oltp_delete

Before starting the workload, load the data as follows:

```sh
sysbench <workload> \
  --pgsql-host=<comma-separated-ips> \
  --tables=20 \
  --table_size=5000000 \
  --range_key_partitioning=false \
  --serial_cache_size=1000 \
  --create_secondary=true \
  --pgsql-db=yugabyte \
  --pgsql-user=yugabyte \
  --db-driver=pgsql \
  --pgsql-port=5433 \
  prepare

```

Run a workload as follows:

```sh
sysbench <workload> \
  --pgsql-host=<comma-separated-ips> \
  --tables=20 \
  --table_size=5000000  \
  --range_key_partitioning=false \
  --serial_cache_size=1000 \
  --create_secondary=true \
  --pgsql-db=yugabyte \
  --pgsql-user=yugabyte \
  --db-driver=pgsql \
  --pgsql-port=5433 \
  --time=1800 \
  --warmup-time=300 \
  --num_rows_in_insert=10 \
  --point_selects=10 \
  --index_updates=10 \
  --non_index_updates=10 \
  --range_selects=false \
  --thread-init-timeout=90 \
  --threads=60 \
  run

```

## Expected results

The following results are for a 3-node cluster running YBDB version {{< yb-version version="v2.20" format="short">}}, with each node running on a c5.2xlarge AWS instance (8 cores, 16 GiB of RAM), all in the same AZ, with a replication factor of 3 and TLS enabled.

### 10 tables each with 100k rows

<table>
  <thead>
    <tr>
      <th rowspan="2">Workload</th>
      <th colspan="2">Benchmark Statistics</th>
      <th colspan="2">Per Query Statistics</th>
      <th rowspan="2">Queries executed in each transaction</th>
    </tr>
    <tr>
      <th>Throughput (txns/sec)</th>
      <th>Latency (ms) - avg</th>
      <th>Throughput (queries/sec)</th>
      <th>Latency (ms) - avg</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>oltp_read_only</td>
      <td>3860.08</td>
      <td>15.54</td>
      <td>38600.8</td>
      <td>1.55</td>
      <td>10 point selects</td>
    </tr>
    <tr>
      <td>oltp_read_write</td>
      <td>233.13</td>
      <td>102.95</td>
      <td>7460.16</td>
      <td>3.21</td>
      <td>10 point selects <br> 10 index updates <br> 10 non-index update <br> 1 Insert <br> 1 Delete</td>
    </tr>
    <tr>
      <td>oltp_multi_insert</td>
      <td>580.12</td>
      <td>41.37</td>
      <td>5801.2</td>
      <td>4.13</td>
      <td>10 Insert</td>
    </tr>
    <tr>
      <td>oltp_update_index</td>
      <td>251.53</td>
      <td>95.41</td>
      <td>2515.3</td>
      <td>9.54</td>
      <td>10 index updates</td>
    </tr>
  </tbody>
</table>

The _Queries executed in each transaction_ column shows the individual queries that are executed as part of each sysbench transaction, for each workload. These queries impact the overall transaction performance and are key to understanding the workload distribution for different sysbench benchmarks.
