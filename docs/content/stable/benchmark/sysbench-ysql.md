---
title: Benchmark YSQL performance using sysbench
headerTitle: sysbench
linkTitle: sysbench
description: Benchmark YSQL performance using sysbench.
headcontent: Benchmark YSQL performance using sysbench
menu:
  stable:
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

Install sysbench on a machine which satisfies the Prerequisites using one of 
the following options:

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
$ sysbench <workload>                       \
      --tables=10                           \
      --table-size=100000                   \
      --range_key_partitioning=true         \
      --db-driver=pgsql                     \
      --pgsql-host=<comma-separated-ips>    \
      --pgsql-port=5433                     \
      --pgsql-user=yugabyte                 \
      --pgsql-db=yugabyte                   \
      prepare
```

Run a workload as follows:

```sh
$ sysbench <workload>                       \
      --tables=10                           \
      --table-size=100000                   \
      --range_key_partitioning=true         \
      --db-driver=pgsql                     \
      --pgsql-host=<comma-separated-ips>    \
      --pgsql-port=5433                     \
      --pgsql-user=yugabyte                 \
      --pgsql-db=yugabyte                   \
      --threads=64                          \
      --time=120                            \
      --warmup-time=120                     \
      run
```

## Expected results

The following results are for a 3-node cluster running YBDB version {{< yb-version version="stable" format="short">}}, with each node running on a c5.2xlarge AWS instance (8 cores, 16 GiB of RAM), all in the same AZ, with a replication factor of 3 and TLS enabled.

### 10 tables each with 100k rows

| Workload          | Throughput(txns/sec) | Latency(ms) |
|------------------------|---------------------------|------------------|
| oltp_read_only         | 46710                    | 1.28             |
| oltp_read_write        | 2480                     | 9.6               |
| oltp_multi_insert      | 5860                     | 4.1              |
| oltp_update_index      | 2610                     | 9.2             |
