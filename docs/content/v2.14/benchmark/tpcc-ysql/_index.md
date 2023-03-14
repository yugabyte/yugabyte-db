---
title: Benchmark YSQL performance using TPC-C
headerTitle: TPC-C
linkTitle: TPC-C
description: Benchmark YSQL performance using TPC-C
headcontent: Benchmark YugabyteDB using TPC-C
image: /images/section_icons/quick_start/explore_ysql.png
menu:
  v2.14:
    identifier: tpcc-ysql
    parent: benchmark
    weight: 4
type: indexpage
showRightNav: true
---

## Overview

Follow the steps below to run the [TPC-C workload](https://github.com/yugabyte/tpcc) against YugabyteDB YSQL. [TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity that are either executed online or queued for deferred execution.

### Results at a glance (Amazon Web Services)

| Warehouses| TPMC | Efficiency (approx) | Cluster Details |
| :-------- |:---- | :------------------ | :-------------- |
| 10    | 127      | 98.75%   | 3 nodes of type `c5d.large` (2 vCPUs) |
| 100   | 1,271.77 | 98.89%   | 3 nodes of type `c5d.4xlarge` (16 vCPUs) |
| 1,000  | 12563.07 | 97.90%   | 3 nodes of type `c5d.4xlarge` (16 vCPUs) |
| 10,000 | 125163.2 | 97.35%   | 30 nodes of type `c5d.4xlarge` (16 vCPUs) |

All the nodes in the cluster were in the same zone. The benchmark VM was the same type as the nodes in the cluster and was deployed in the same zone as the DB cluster. Each test was run for 30 minutes after the loading of the data.

### Results at a glance (Microsoft Azure)

| Warehouses | TPMC | Efficiency (approx) | Cluster details |
| :--------- | :--- | :-----------------: | :-------------- |
| 50 | 639.4 | 99.44% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 100 | 1,271.37 | 98.86% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 1000 | 12,523.97 | 97.39% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |
| 2000 | 25,407.43 | 98.78% | 3 nodes of type `D16 v3` (16 vCPUs) with `P40` disks |

All nodes in the cluster were in the same zone. The benchmark VM was the same type as the nodes in the cluster, and was deployed in the same zone as the DB cluster. Each test was run for 30 minutes after the loading of the data.

## Prerequisites

### Get TPC-C binaries

To download the TPC-C binaries, run the following commands.

```sh
$ wget https://github.com/yugabyte/tpcc/releases/download/2.0/tpcc.tar.gz
$ tar -zxvf tpcc.tar.gz
$ cd tpcc
```

### Start the database

Start your YugabyteDB cluster by following the steps for a [manual deployment](../../deploy/manual-deployment/).

{{< tip title="Tip" >}}
You will need the IP addresses of the nodes in the cluster for the next step.
{{< /tip>}}

## Configure DB connection parameters (optional)

Workload configuration like IP addresses of the nodes, number of warehouses and number of loader threads can be controlled by command line arguments.
Other options like username, password, port, etc. can be changed using the configuration file at `config/workload_all.xml`, if needed.

```xml
<port>5433</port>
<username>yugabyte</username>
<password></password>
```

## Best practices

**Latest TPCC code:** Use the latest enhancements to the Yugabyte TPCC application. You can either download the latest released version, or you can clone the repository and build from source to get the very latest changes.

**Pre-compacting tables:** Pre-compact tables with the [yb-admin](../../admin/yb-admin/) utility's `compact_table` command.

**Warming the database:** Use the `--warmup-time-secs` flag when you call the execute phase of the TPCC benchmark.

## Run the TPC-C benchmark

### Load phase

{{< tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

Before starting the workload, you need to load the data. Make sure to replace the IP addresses with that of the nodes in the cluster.

```sh
$ ./tpccbenchmark --create=true --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

```sh
$ ./tpccbenchmark --load=true --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

| Cluster | Loader threads | Loading time | Data set size |
| :------ | :------------: | :----------- | :------------ |
| 3 nodes, type `c5d.large` | 10 | ~13 minutes | ~20 GB |

The loading time for ten warehouses on a cluster with 3 nodes of type `c5d.4xlarge` is approximately 3 minutes.

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

Before starting the workload, you need to load the data. Make sure to replace the IP addresses with that of the nodes in the cluster. Loader threads allow you to configure the number of threads used to load the data. For a 3-node c5d.4xlarge cluster, loader threads value of 48 was optimal.

```sh
$ ./tpccbenchmark --create=true --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

```sh
$ ./tpccbenchmark --load=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100 \
  --loaderthreads 48
```

| Cluster | Loader threads | Loading time | Data set size |
| :------ | :------------- | :----------- | :------------ |
| 3 nodes, type `c5d.4xlarge` | 48 | ~20 minutes | ~80 GB |

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The specified 48 threads value is optimal for a 3-node cluster of type `c5d.4xlarge` (16 vCPUs). For larger clusters or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

Before starting the workload, you need to load the data first. Make sure to replace the IP addresses with that of the nodes in the cluster. Loader threads allow you to configure the number of threads used to load the data. For a 3-node `c5d.4xlarge` cluster, loader threads value of 48 was optimal.

```sh
$ ./tpccbenchmark --create=true --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

```sh
$ ./tpccbenchmark --load=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=1000 \
  --loaderthreads 48
```

| Cluster | Loader threads | Loading time | Data set size |
| :------ | :------------- | :----------- | :------------ |
| 3 nodes, type `c5d.4xlarge` | 48 | ~3.5 hours | ~420 GB |

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The specified 48 threads value is optimal for a 3-node cluster of type c5d.4xlarge (16 vCPUs). For larger clusters or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

Before starting the workload, you need to load the data. In addition, you need to ensure that you exported a list of all IP addresses of all the nodes involved.

For 10k warehouses, you would need ten clients of type `c5.4xlarge` to drive the benchmark. For multiple clients, you need to perform three steps.

First, you create the database and the corresponding tables. Execute the following command from one of the clients:

```sh
./tpccbenchmark  --nodes=$IPS  --create=true
```

Once the database and tables are created, you can load the data from all ten clients:

| Client | Command |
| -----: | :------ |
| 1  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=1    --total-warehouses=10000 --loaderthreads 48 |
| 2  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=1001 --total-warehouses=10000 --loaderthreads 48 |
| 3  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=2001 --total-warehouses=10000 --loaderthreads 48 |
| 4  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=3001 --total-warehouses=10000 --loaderthreads 48 |
| 5  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=4001 --total-warehouses=10000 --loaderthreads 48 |
| 6  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=5001 --total-warehouses=10000 --loaderthreads 48 |
| 7  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=6001 --total-warehouses=10000 --loaderthreads 48 |
| 8  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=7001 --total-warehouses=10000 --loaderthreads 48 |
| 9  | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=8001 --total-warehouses=10000 --loaderthreads 48 |
| 10 | ./tpccbenchmark --load=true --nodes=$IPS --warehouses=1000 --start-warehouse-id=9001 --total-warehouses=10000 --loaderthreads 48 |

Tune the `--loaderthreads` parameter for higher parallelism during the load, based on the number and type of nodes in the cluster. The value specified here, 48 threads, is optimal for a 3-node cluster of type `c5d.4xlarge` (16 vCPUs). For larger clusters, or computers with more vCPUs, increase this value accordingly. For clusters with a replication factor of 3, a good approximation is to use the number of cores you have across all the nodes in the cluster.

Once the loading is completed, execute the following command to enable the foreign keys that were disabled to aid the loading times:

```sh
./tpccbenchmark  --nodes=$IPS  --enable-foreign-keys=true
```

| Cluster | Loader threads | Loading time | Data set size |
| :------ | :------------- | :----------- | :------------ |
| 30 nodes, type `c5d.4xlarge` | 480 | ~5.5 hours | ~4 TB |

{{% /tab %}}
{{< /tabpane >}}

### TPC-C Execute Phase

{{< tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3
```

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=100
```

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

You can run the workload against the database as follows:

```sh
$ ./tpccbenchmark --execute=true \
  --nodes=127.0.0.1,127.0.0.2,127.0.0.3 \
  --warehouses=1000
```

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

Before starting the execution, you have to move all the tablet leaders out of the node containing the master leader by running the following command:

```sh
./yb-admin --master_addresses <master-ip1>:7100,<master-ip2>:7100,<master-ip3>:7100 change_leader_blacklist ADD <master-leader-ip>
```

Make sure that the IP addresses used in the execution phase don't include the `master-leader-ip`.
You can then run the workload against the database from each client:

| Client | Command |
| -----: | :------ |
| 1  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=1    --total-warehouses=10000 --warmup-time-secs=900 |
| 2  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=1001 --total-warehouses=10000 --warmup-time-secs=900 |
| 3  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=2001 --total-warehouses=10000 --warmup-time-secs=900 |
| 4  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=3001 --total-warehouses=10000 --warmup-time-secs=900 |
| 5  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=4001 --total-warehouses=10000 --warmup-time-secs=900 |
| 6  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=5001 --total-warehouses=10000 --warmup-time-secs=720 --initial-delay-secs=180 |
| 7  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=6001 --total-warehouses=10000 --warmup-time-secs=540 --initial-delay-secs=360 |
| 8  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=7001 --total-warehouses=10000 --warmup-time-secs=360 --initial-delay-secs=540 |
| 9  | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=8001 --total-warehouses=10000 --warmup-time-secs=180 --initial-delay-secs=720 |
| 10 | ./tpccbenchmark  --nodes=$IPS --execute=true --warehouses=1000 --num-connections=300 --start-warehouse-id=9001 --total-warehouses=10000 --warmup-time-secs=0   --initial-delay-secs=900 |

{{% /tab %}}
{{< /tabpane >}}

### TPC-C Benchmark Results

{{< tabpane text=true >}}
{{% tab header="10 warehouses" lang="10-wh" %}}

**Cluster**: 3 nodes of type `c5d.large`

**TPMC**: 127

**Efficiency**: 98.75%

**Latencies**:

* **New Order** Avg: 66.286 msecs, p99: 212.47 msecs
* **Payment** Avg: 17.406 msecs, p99: 186.884 msecs
* **OrderStatus** Avg: 7.308 msecs, p99: 86.974 msecs
* **Delivery** Avg: 66.986 msecs, p99: 185.919 msecs
* **StockLevel** Avg: 98.32 msecs, p99: 192.054 msecs

Once the execution is completed, the TPM-C number along with the efficiency is printed, as follows:

```output
21:09:23,588 (DBWorkload.java:955) INFO  - Throughput: Results(nanoSeconds=1800000263504, measuredRequests=8554) = 4.752221526539232 requests/sec reqs/sec
21:09:23,588 (DBWorkload.java:956) INFO  - Num New Order transactions : 3822, time seconds: 1800
21:09:23,588 (DBWorkload.java:957) INFO  - TPM-C: 127
21:09:23,588 (DBWorkload.java:958) INFO  - Efficiency : 98.75%
21:09:23,593 (DBWorkload.java:983) INFO  - NewOrder, Avg Latency: 66.286 msecs, p99 Latency: 212.47 msecs
21:09:23,596 (DBWorkload.java:983) INFO  - Payment, Avg Latency: 17.406 msecs, p99 Latency: 186.884 msecs
21:09:23,596 (DBWorkload.java:983) INFO  - OrderStatus, Avg Latency: 7.308 msecs, p99 Latency: 86.974 msecs
21:09:23,596 (DBWorkload.java:983) INFO  - Delivery, Avg Latency: 66.986 msecs, p99 Latency: 185.919 msecs
21:09:23,596 (DBWorkload.java:983) INFO  - StockLevel, Avg Latency: 98.32 msecs, p99 Latency: 192.054 msecs
21:09:23,597 (DBWorkload.java:792) INFO  - Output Raw data into file: results/oltpbench.csv
```

{{% /tab %}}
{{% tab header="100 warehouses" lang="100-wh" %}}

**Cluster**: 3 nodes of type `c5d.4xlarge`

**TPMC**: 1271.77

**Efficiency**: 98.89%

**Latencies**:

* **New Order** Avg: 68.265 msecs, p99: 574.339 msecs
* **Payment** Avg: 19.969 msecs, p99: 475.311 msecs
* **OrderStatus** Avg: 13.821 msecs, p99: 571.414 msecs
* **Delivery** Avg: 67.384 msecs, p99: 724.67 msecs
* **StockLevel** Avg: 114.032 msecs, p99: 263.849 msecs

Once the execution is completed, the TPM-C number along with the efficiency is printed, as follows:

```output
04:54:54,560 (DBWorkload.java:955) INFO  - Throughput: Results(nanoSeconds=1800000866600, measuredRequests=85196) = 47.33108832382159 requests/sec reqs/sec
04:54:54,560 (DBWorkload.java:956) INFO  - Num New Order transactions : 38153, time seconds: 1800
04:54:54,560 (DBWorkload.java:957) INFO  - TPM-C: 1,271.77
04:54:54,560 (DBWorkload.java:958) INFO  - Efficiency : 98.89%
04:54:54,596 (DBWorkload.java:983) INFO  - NewOrder, Avg Latency: 68.265 msecs, p99 Latency: 574.339 msecs
04:54:54,615 (DBWorkload.java:983) INFO  - Payment, Avg Latency: 19.969 msecs, p99 Latency: 475.311 msecs
04:54:54,616 (DBWorkload.java:983) INFO  - OrderStatus, Avg Latency: 13.821 msecs, p99 Latency: 571.414 msecs
04:54:54,617 (DBWorkload.java:983) INFO  - Delivery, Avg Latency: 67.384 msecs, p99 Latency: 724.67 msecs
04:54:54,618 (DBWorkload.java:983) INFO  - StockLevel, Avg Latency: 114.032 msecs, p99 Latency: 263.849 msecs
04:54:54,619 (DBWorkload.java:792) INFO  - Output Raw data into file: results/oltpbench.csv
```

{{% /tab %}}
{{% tab header="1,000 warehouses" lang="1k-wh" %}}

**Cluster**: 3 nodes of type `c5d.4xlarge`

**TPMC**: 12,563.07

**Efficiency**: 97.69%

**Latencies**:

* **New Order** Avg: 325.378 msecs, p99: 3758.859 msecs
* **Payment** Avg: 277.539 msecs, p99: 12667.048 msecs
* **OrderStatus** Avg: 174.173 msecs, p99: 4968.783 msecs
* **Delivery** Avg: 310.19 msecs, p99: 5259.951 msecs
* **StockLevel** Avg: 652.827 msecs, p99: 8455.325 msecs

Once the execution is completed, the TPM-C number along with the efficiency is printed, as follows:

```output
17:18:58,728 (DBWorkload.java:955) INFO  - Throughput: Results(nanoSeconds=1800000716759, measuredRequests=842216) = 467.8975914612168 requests/sec reqs/sec
17:18:58,728 (DBWorkload.java:956) INFO  - Num New Order transactions : 376892, time seconds: 1800
17:18:58,728 (DBWorkload.java:957) INFO  - TPM-C: 12,563.07
17:18:58,728 (DBWorkload.java:958) INFO  - Efficiency : 97.69%
17:18:59,006 (DBWorkload.java:983) INFO  - NewOrder, Avg Latency: 325.378 msecs, p99 Latency: 3758.859 msecs
17:18:59,138 (DBWorkload.java:983) INFO  - Payment, Avg Latency: 277.539 msecs, p99 Latency: 12667.048 msecs
17:18:59,147 (DBWorkload.java:983) INFO  - OrderStatus, Avg Latency: 174.173 msecs, p99 Latency: 4968.783 msecs
17:18:59,166 (DBWorkload.java:983) INFO  - Delivery, Avg Latency: 310.19 msecs, p99 Latency: 5259.951 msecs
17:18:59,182 (DBWorkload.java:983) INFO  - StockLevel, Avg Latency: 652.827 msecs, p99 Latency: 8455.325 msecs
17:18:59,183 (DBWorkload.java:792) INFO  - Output Raw data into file: results/oltpbench.csv
```

{{% /tab %}}
{{% tab header="10,000 warehouses" lang="10k-wh" %}}

When the execution is completed, you need to copy the `csv` files from each of the nodes to one of the nodes and run `merge-results` to display the merged results.

Once you copied the `csv` files to a directory such as `results-dir`, you can merge the results as follows:

```sh
./tpccbenchmark --merge-results=true --dir=results-dir --warehouses=10000
```

**Cluster**: 30 nodes of type `c5d.4xlarge`

**TPMC**: 125193.2

**Efficiency**: 97.35%

**Latencies**:

* **New Order** Avg: 114.639 msecs, p99: 852.183 msecs
* **Payment** Avg: 114.639 msecs, p99 : 852.183 msecs
* **OrderStatus** Avg: 20.86 msecs, p99: 49.31 msecs
* **Delivery** Avg: 117.473 msecs, p99: 403.404 msecs
* **StockLevel** Avg: 340.232 msecs, p99: 1022.881 msecs

The output after merging should look similar to the following:

```output
15:16:07,397 (DBWorkload.java:715) INFO - Skipping benchmark workload execution
15:16:11,400 (DBWorkload.java:1080) INFO - Num New Order transactions : 3779016, time seconds: 1800
15:16:11,400 (DBWorkload.java:1081) INFO - TPM-C: 125193.2
15:16:11,401 (DBWorkload.java:1082) INFO - Efficiency : 97.35%
15:16:12,861 (DBWorkload.java:1010) INFO - NewOrder, Avg Latency: 114.639 msecs, p99 Latency: 852.183 msecs
15:16:13,998 (DBWorkload.java:1010) INFO - Payment, Avg Latency: 29.351 msecs, p99 Latency: 50.8 msecs
15:16:14,095 (DBWorkload.java:1010) INFO - OrderStatus, Avg Latency: 20.86 msecs, p99 Latency: 49.31 msecs
15:16:14,208 (DBWorkload.java:1010) INFO - Delivery, Avg Latency: 117.473 msecs, p99 Latency: 403.404 msecs
15:16:14,310 (DBWorkload.java:1010) INFO - StockLevel, Avg Latency: 340.232 msecs, p99 Latency: 1022.881 msecs
```

{{% /tab %}}
{{< /tabpane >}}
