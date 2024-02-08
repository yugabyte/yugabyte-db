---
title: Large datasets
headerTitle: Large datasets
linkTitle: Large datasets
description: Large datasets
headcontent: Performance running large datasets when scaling out
menu:
  preview:
    name: Large datasets
    identifier: scaling-large-datasets
    parent: explore-scalability
    weight: 80
type: docs
---

With ever-growing data workloads, such as time series metrics and IoT sensor events, databases need to handle large data (larger than 10 TB). Running a highly dense database cluster where each node stores terabytes of data makes perfect sense from a cost-efficiency standpoint. But spinning up new data nodes to get more storage-per-node leads to significant wastage of expensive compute resources.

YugabyteDB has been designed from the ground up to handle large data and is powered by a radical replication and [storage architecture](../../../architecture/docdb/). Bootstrapping new nodes and removing existing nodes is much simpler and more resilient in YugabyteDB when compared to the eventually-consistent Cassandra-compatible databases.

The following scenario shows how YugabyteDB handles a total of 18 TB on just four general-purpose machines.

## Configuration and data size

|                                |                                                             |
| -----------------------------: | ----------------------------------------------------------- |
| Cluster Size                   | **4**                                                       |
| Node Type                      | c4.4xlarge (16-vcpus, 30 GB RAM, 1 x 6000 TB gp2 EBS SSD)   |
| Replication Factor             | 3                                                           |
| Number of KV records           | 20 Billion                                                  |
| Key + Value Size               | ~300 Bytes                                                  |
| Key size                       | 50 Bytes                                                    |
| Value size                     | 256 Bytes (deliberately chosen to be not very compressible) |
| Logical Data Set Size          | 20 Billion keys * 300 Bytes = 6000 GB                       |
| Raw Data including Replication | 6000 GB * 3 = **18 TB**                                     |
| Data Per Node                  | 18TB / 4 = **4.5 TB**                                       |

## Data load

The data was loaded at a steady rate over about 6 days using the [Cassandra Key Value](../../../benchmark/key-value-workload-ycql/) sample application. The following graph shows the steady growth in SSTable size per node for 6 days, beyond which it stabilizes at 4.5 TB.

![Data load](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture1-1.png)

The following figure shows the YB-Master Admin UI, with the tablet servers, number of tablets on each, number of tablet leaders, and size of the on-disk SSTable files (4.5 TB).

![SST Size](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture2-1.png)

## Read-heavy workload

On a read-heavy workload with 32 readers and 2 writers, YugabyteDB serviced 19K ops/s with a latency of 13.5 ms.

{{<table>}}

|                                     Ops/s                            | Latency |
| :------------------------------------------------------------------: | :-----: |
| ![Ops/s](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture3-1.png)
| ![Latency](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture4-1.png) |

{{</table>}}

Note that this is a random-read workload, with only 30GB of RAM for 4.5TB of data per node. So every read will be forced to go to disk, and this workload is bottlenecked by the IO the EBS volume can support.

As expected, CPU was not the bottleneck in this workload, and CPU use was under 7%.

## Write-heavy workload

On a write-heavy workload with 2 readers and 64 writers, YugabyteDB serviced 25K ops/s with a latency of 4 ms.

{{<table>}}
|                                     Ops/s                            | Latency |
| :------------------------------------------------------------------: | :-----: |
| ![Ops/s](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture11.png)
| ![Latency](https://www.yugabyte.com/wp-content/uploads/2018/08/Picture12.png) |
{{</table>}}

For a write-heavy workload, there is more variability because of the use of general-purpose SSD (gp2) EBS volumes. This variability arises from the need to perform periodic compactions. Note that for time series data such as IoT or KairosDB-like workloads, YugabyteDB doesn't need to perform aggressive compactions, and the results will be even better. But this particular experiment is running an arbitrary KV workload, and as expected puts a higher demand on the system.

Instead of using 1x6000GB EBS volume per node, using 2x3000GB EBS volumes might be a better choice to get more throughput. When the disk R + W throughput reaches its maximum (around 150MB/sec) during compactions, the latencies also go up a bit.

To allow sufficient room for foreground operations on a low-end gp2 SSD volume-based setup, we reduced the default quota settings for compaction/flush rate from 100MB/sec to 30MB/sec.

On i3* instance types with directly attached NVMe SSDs (which have much more disk IOPS and throughput), this observed variability should be minimal for a similar workload.

## Customer use cases

### 20TB

In a customer installation, Yugabyte DB has been loaded with `~20 TB` of compressed data (`~50TB` uncompressed) per node and Yugabyte DB was able to handle sustained `450K ops/sec` with CPU use at `~40%` maximum.

### 80TB

One of our customers uses YCQL interface to store and retrieve about `80TB` of event data from different vehicles clocked at `1.25 M Ops/s` with an average latency of `1.7s`. Their setup is:

|                          |             |
| -----------------------: | :---------- |
|                    Nodes | 40          |
|                    Zones | 5           |
|       Replication Factor | 5           |
|                      CPU | 64vCPU      |
|                   Memory | 256GB RAM   |
|                  Storage | 6x 4TB NVMe |
|                Data Size | **80 TB**   |
|        Data Density/node | **10 TB**   |
| Data Size (+Replication) | **450TB**   |

## Operational efficiency

Scaling up and down with Yugabyte DB doesn't result in any loss of availability and the data loader application continues to perform high ingest in parallel without any interruption. As YugabyteDB performs size-tiered compactions automatically in the background, explicit manual compaction is not needed and there is only 10-20% space amplification overhead.

## Learn more

- [YugabyteDB Performance Benchmarks](../../../benchmark/)
