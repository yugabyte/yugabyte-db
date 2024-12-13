---
title: Benchmark YSQL performance using sysbench
headerTitle: sysbench
linkTitle: sysbench
description: Benchmark YSQL performance using sysbench.
headcontent: Benchmark YSQL performance using sysbench
menu:
  preview:
    identifier: sysbench-ysql
    parent: benchmark
    weight: 5
aliases:
  - /benchmark/sysbench/
type: docs
---

sysbench is a popular tool for benchmarking databases like PostgreSQL and MySQL, as well as system capabilities like CPU, memory, and I/O. The [YugabyteDB version of sysbench](https://github.com/yugabyte/sysbench) is forked from the [official version](https://github.com/akopytov/sysbench) with a few modifications to better reflect YugabyteDB's distributed nature.

## Running the benchmark

### Prerequisites

To ensure the recommended hardware requirements are met and the database is correctly configured before benchmarking, review the [deployment checklist](../../deploy/checklist/).

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
{{% readfile "./github.md" %}}
  </div>

  <div id="rhel" class="tab-pane fade" role="tabpanel" aria-labelledby="rhel-tab">
{{% readfile "./rhel.md" %}}
  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">
{{% readfile "./macos.md" %}}
  </div>

</div>

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
$ sysbench <workload>               			\
      --tables=10                   		\
      --table-size=100000          			\
      --range_key_partitioning=true 			\
      --db-driver=pgsql             			\
      --pgsql-host=<comma-separated-ips>        	\
      --pgsql-port=5433             			\
      --pgsql-user=yugabyte         			\
      --pgsql-db=yugabyte           			\
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

The following results are for a 3-node cluster, with each node running on a c5.2xlarge AWS instance (8 cores, 16 GiB of RAM), all in the same AZ, with a replication factor of 3 and TLS enabled.

### 10 tables each with 100k rows

| Workload              | Throughput(txns/sec) | Latency(ms) |
| --------------------- | -------------------- | ----------- |
| oltp_read_only        | 36150                | 1.6         |
| oltp_read_write       | 2150                 | 11.1        |
| oltp_multi_insert     | 5450                 | 4.4         |
| oltp_update_index     | 2420                 | 9.8         |
| oltp_update_non_index |                      |             |
| oltp_delete           |                      |             |
