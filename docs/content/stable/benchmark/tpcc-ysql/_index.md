---
title: Benchmark YSQL performance using TPC-C
headerTitle: TPC-C
linkTitle: TPC-C
description: Benchmark YSQL performance using TPC-C
headcontent: Benchmark YugabyteDB using TPC-C
image: /images/section_icons/quick_start/explore_ysql.png
menu:
  stable:
    identifier: tpcc-ysql
    parent: benchmark
    weight: 4
showAsideToc: true
isTocNested: true
---

## Overview
Follow the steps below to run the [TPC-C workload](https://github.com/yugabyte/tpcc) against YugabyteDB YSQL. [TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity that are either executed online or queued for deferred execution.

### Results at a glance (Amazon Web Services)
| Warehouses| TPMC | Efficiency (approx) | Cluster Details
-------------|-----------|------------|------------|
10    | 127      | 98.75%   | 3 nodes of type `c5d.large` (2 vCPUs)
100   | 1,271.77 | 98.89%   | 3 nodes of type `c5d.4xlarge` (16 vCPUs)
1000  | 12563.07 | 97.90%   | 3 nodes of type `c5d.4xlarge` (16 vCPUs)
10000 | 125163.2 | 97.35%   | 30 nodes of type `c5d.4xlarge` (16 vCPUs)

All the nodes in the cluster were in the same zone. The benchmark VM was the same type as the nodes in the cluster and was deployed in the same zone as the DB cluster. Each test was run for 30 minutes after the loading of the data.

### Results at a glance (Microsoft Azure)

| Warehouses | TPMC | Efficiency (approx) | Cluster details |
| :--------- | :--- | :-----------------: | :-------------- |
| 50 | 639.4 | 99.44% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 100 | 1,271.37 | 98.86% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 1000 | 12,523.97 | 97.39% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 2000 | 25,407.43 | 98.78% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |

All nodes in the cluster were in the same zone. The benchmark VM was the same type as the nodes in the cluster, and was deployed in the same zone as the DB cluster. Each test was run for 30 minutes after the loading of the data.

## 1. Prerequisites

### Get TPC-C binaries

To download the TPC-C binaries, run the following commands.

```sh
$ wget https://github.com/yugabyte/tpcc/releases/download/2.0/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

### Start the Database

Start your YugabyteDB cluster by following the steps [here](../../deploy/manual-deployment/).

{{< tip title="Tip" >}}
You will need the IP addresses of the nodes in the cluster for the next step.
{{< /tip>}}


## 2. Configure DB connection params (optional)

Workload configuration like IP addresses of the nodes, number of warehouses and number of loader threads can be controlled by command line arguments.
Other options like username, password, port, etc. can be changed using the configuration file at `config/workload_all.xml`, if needed.

```sh
<port>5433</port>
<username>yugabyte</username>
<password></password>
```

## 3. Best practices

**Latest TPCC code:** Use the latest enhancements to the Yugabyte TPCC application. You can either download the latest released version, or, to get the very latest changes, you can clone the repository and build from source.

**Pre-compacting tables:** Pre-compact tables with the yb-admin utility's `compact_table` command.

**Warming the database:** Use the `--warmup-time-secs` flag when you call the execute phase of the TPCC benchmark.

## 4. Run the TPC-C benchmark

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#10-wh" class="nav-link" id="10-wh-tab" data-toggle="tab" role="tab" aria-controls="10-wh" aria-selected="true">
      10 Warehouses
    </a>
  </li>
  <li>
    <a href="#100-wh" class="nav-link active" id="100-wh-tab" data-toggle="tab" role="tab" aria-controls="100-wh" aria-selected="false">
      100 Warehouses
    </a>
  </li>
  <li>
    <a href="#1000-wh" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      1000 Warehouses
    </a>
  </li>
  <li>
    <a href="#10000-wh" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      10,000 Warehouses
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="10-wh" class="tab-pane fade" role="tabpanel" aria-labelledby="10-wh-tab">
    {{% includeMarkdown "10-wh/tpcc-ysql.md" /%}}
  </div>
  <div id="100-wh" class="tab-pane fade show active" role="tabpanel" aria-labelledby="100-wh-tab">
    {{% includeMarkdown "100-wh/tpcc-ysql.md" /%}}
  </div>
  <div id="1000-wh" class="tab-pane fade" role="tabpanel" aria-labelledby="1000-wh-tab">
    {{% includeMarkdown "1000-wh/tpcc-ysql.md" /%}}
  </div>
  <div id="10000-wh" class="tab-pane fade" role="tabpanel" aria-labelledby="10000-wh-tab">
    {{% includeMarkdown "10000-wh/tpcc-ysql.md" /%}}
  </div>
</div>

