---
title: TPC-C Benchmark on YugabyteDB
headerTitle: TPC-C Benchmark on YugabyteDB
linkTitle: TPC-C
description: Benchmark YugabyteDB using TPC-C.
image: /images/section_icons/explore/high_performance.png
aliases:
  - /benchmark/tpcc
  - /benchmark/tpcc-ysql
menu:
  preview:
    identifier: tpcc
    parent: benchmark
    weight: 4
type: indexpage
---

[TPC-C](http://www.tpc.org/tpcc/) is a popular online transaction processing benchmark that provides metrics you can use to evaluate the performance of YugabyteDB for concurrent transactions of different types and complexity, and which are either executed online or queued for deferred execution. Developed by the Transaction Processing Performance Council (TPC), it simulates a complete computing environment where a population of users executes transactions against a database.

## Benchmark workflow

1. The benchmark starts by initializing the database with a specific number of warehouses, each containing multiple districts, customers, orders, and items. The size of the database is proportional to the number of warehouses, ensuring scalability.
1. The database tables are populated with initial data, including warehouses, districts, customers, orders, stock, and items.
1. Before the actual measurement begins, the system undergoes a warm-up period. Transactions are executed to populate caches and buffers, bringing the system to a steady-state operation.
1. During the measurement phase, a mix of the five transaction types (New-Order, Payment, Order-Status, Delivery, and Stock-Level) is executed. The transactions are executed according to the predefined distribution:
    - New-Order: 45%
    - Payment: 43%
    - Order-Status: 4%
    - Delivery: 4%
    - Stock-Level: 4%
1. The primary metric is the number of New-Order transactions per minute (tpmC). The system must meet specific response time requirements for transactions: 90% of New-Order transactions must be completed within 5 seconds. Other transactions have similar response time requirements to ensure the system performs well under load.

## Horizontal scaling

The TPC-C workload on YugabyteDB scales linearly when increasing the number of nodes. Here is the chart showing how the transactions per minute (tpmC) scales linearly with an efficiency score of 99.7% as we increase the number of nodes in the cluster of `m6i.2xlarge` instances (8 vCPUs).

| Warehouses |   TPMC   | Efficiency(%) | Nodes | Connections | New Order Latency | Machine Type (vCPUs) |
| ---------: | :------- | :-----------: | :---: | ----------- | :---------------: | :------------------- |
|        500 | 25646.4  |     99.71     |   3   | 200         |     54.21 ms      | m6i.2xlarge&nbsp;(8) |
|       1000 | 34212.57 |     99.79     |   4   | 266         |     53.92 ms      | m6i.2xlarge&nbsp;(8) |
|       2000 | 42772.6  |     99.79     |   5   | 333         |     51.01 ms      | m6i.2xlarge&nbsp;(8) |
|       4000 | 51296.9  |     99.72     |   6   | 400         |     62.09 ms      | m6i.2xlarge&nbsp;(8) |

![Horizontal scaling](/images/benchmark/tpcc-horizontal.png)

## Scaling CPUs

The TPC-C workload on YugabyteDB scales linearly with an increase in the number of CPUs on the nodes. Here is the chart showing how the transactions per minute (tpmC) doubles with an efficiency score of 99.8%  as we double the number of CPUs in a 3-node cluster.


| Warehouses |   TPMC   | Efficiency(%) | Nodes | Connections | New Order Latency | Machine Type (vCPUs)  |
| ---------: | :------- | :-----------: | :---: | ----------- | :---------------: | :-------------------- |
|        500 | 6415.7   |     99.78     |   3   | 50          |     64.08 ms      | m6i.large&nbsp;(2)    |
|       1000 | 12829.93 |     99.77     |   3   | 100         |     73.97 ms      | m6i.xlarge&nbsp;(4)   |
|       2000 | 25646.4  |     99.78     |   3   | 200         |     54.21 ms      | m6i.2xlarge&nbsp;(8)  |
|       4000 | 51343.5  |     99.81     |   3   | 400         |     39.46 ms      | m6i.4xlarge&nbsp;(16) |

![Vertical scaling](/images/benchmark/tpcc-vertical.png)

## Max scale tested

In our testing, YugabyteDB was able to process 1M tpmC with 150,000 warehouses at an efficiency of 99.8% on an RF3 cluster of 75 c5d.12xlarge machines with a total data size of 50TB.

| Warehouses | TPMC | Efficiency(%) | Nodes | Connections | New Order Latency |  Machine Type (vCPUs)  |
| ---------: | :--- | :-----------: | :---: | ----------- | :---------------: | :--------------------- |
|    150,000 | 1M   |     99.30     |  75   | 9000        |     123.33 ms     | c5d.12xlarge&nbsp;(48) |

## Run the benchmark

{{<lead link="running-tpcc/">}}
To run the TPC-C against a local or a YB Managed cluster, see [Running TPC-C](running-tpcc/)
{{</lead>}}