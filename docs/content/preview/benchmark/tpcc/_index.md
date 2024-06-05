---
title: TPC-C Benchmark on YugabyteDB
headerTitle: TPC-C Benchmark on YugabyteDB
linkTitle: TPC-C
description: Benchmark YugabyteDB using TPC-C.
image: /images/section_icons/explore/high_performance.png
aliases:
  - /benchmark/tpcc
  - /benchmark/tpcc-ysql
  - /preview/benchmark/tpcc-ysql/
menu:
  preview:
    identifier: tpcc
    parent: benchmark
    weight: 4
type: indexpage
---

[TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity, and which are either executed online or queued for deferred execution. Developed by the Transaction Processing Performance Council (TPC), it simulates a complete computing environment where a population of users execute transactions against a database.

{{<note>}}
All benchmarks were run on a single-region YugabyteDB cluster running on {{<release "2.18.1">}}, except 150K warehouses, which was run on [v2.11](../../releases/ybdb-releases/end-of-life/v2.11/).
{{</note>}}

## Running the benchmark

Conducting an accurate TPC-C benchmark requires aligning your test environment with your production landscape. Begin by assessing your anticipated workload in terms of IOPS and projected data volume. These estimates will guide you in selecting an appropriate cluster configuration that closely mirrors your operational requirements.

After you've identified a cluster specification that matches your needs, apply the TPC-C workload recommended for that particular setup. The goal is to validate that the cluster can sustain the expected transaction throughput—measured in tpmC with a high degree of efficiency, typically exceeding 99.5%. This high-efficiency rate ensures that the cluster meets the benchmark's demands with minimal resource overhead, indicating its readiness to handle your real-world, high-volume transactional workloads.

{{<lead link="running-tpcc/">}}
For information on cluster specification/workload and how to run the TPC-C against a local or a YugabyteDB Managed cluster, see [Running TPC-C](running-tpcc/).
{{</lead>}}

## Scale out

YugabyteDB exhibits exemplary scalability under the TPC-C workload, demonstrating a linear growth in performance as the cluster expands. The accompanying graph illustrates this linear scalability, showing how YugabyteDB's transaction throughput—quantified in tpmC increases in direct proportion to the number of nodes added to the cluster.

![Horizontal scaling](/images/benchmark/tpcc-horizontal.png)

{{<lead link="running-tpcc/">}}
To see how effectively YugabyteDB handles the TPC-C workload while scaling out, see [Testing horizontal scaling](horizontal-scaling/).
{{</lead>}}

## High scale workloads

YugabyteDB's robust performance in the TPC-C benchmark, particularly when scaled to a high number of warehouses, serves as a compelling testament to its prowess in handling high-volume transaction processing workloads. By excelling in this industry-standard test, which simulates complex, concurrent transactions across a vast, distributed dataset, YugabyteDB has effectively demonstrated its ability to manage the intense demands of large-scale OLTP environments.

{{<lead link="high-scale-workloads/">}}
To see how well YugabyteDB handles extremely high workloads, see [Testing high scale workloads](high-scale-workloads/).
{{</lead>}}

## Max scale tested

In our testing, YugabyteDB was able to process 1M tpmC with 150,000 warehouses at an efficiency of 99.8% on an RF3 cluster of 75 c5d.12xlarge machines with a total data size of 50TB.

{{<note>}}
The 150K warehouses benchmark was run on [v2.11](../../releases/ybdb-releases/end-of-life/v2.11/).
{{</note>}}

| Warehouses | TPMC | Efficiency(%) | Nodes | Connections | New Order Latency |  Machine Type (vCPUs)  |
| ---------: | :--- | :-----------: | :---: | ----------- | :---------------: | :--------------------- |
|    150,000 | 1M   |     99.30     |  75   | 9000        |     123.33 ms     | c5d.12xlarge&nbsp;(48) |

{{<lead link="high-scale-workloads/">}}
To know more about this accomplishment, see [Largest benchmark](./high-scale-workloads/#largest-benchmark).
{{</lead>}}
