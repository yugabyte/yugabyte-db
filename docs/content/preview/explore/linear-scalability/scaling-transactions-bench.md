---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions
headcontent: Transaction performance when scaling out
menu:
  preview:
    name: Transactions
    identifier: scaling-transactions-bench
    parent: explore-scalability
    weight: 60
type: docs
---

The [Transaction Processing System Benchmark(TPC-C)](https://www.tpc.org/tpcc/detail5.asp) is the gold standard for measuring the transaction processing capacity of a database. It simulates order entry for a wholesale parts supplier. It includes a mixture of transaction types, including the following:

- Entering and delivering orders
- Recording payments
- Checking order status
- Monitoring stock levels

The performance metric measures the number of new orders that can be processed per minute and is expressed in transactions per minute (TPM-C). The benchmark is designed to simulate business expansion by increasing the number of warehouses.

## 100K Warehouse

The following shows the results for the TPC-C benchmark for 100K warehouses.

| Cluster configuration |             |
| --------------------- | ----------- |
| YugabyteDB Version    | 2.18.0      |
| Instance Type         | c5d.9xlarge |
| Provider Type         | AWS         |
| Nodes                 | 59          |
| RF                    | 3           |
| Region                | us-west     |

| Results                  |            |
| ------------------------ | ---------- |
| Efficiency               | 99.83      |
| TPMC                     | 1283804.18 |
| Average NewOrder Latency | 51.86 ms   |
| YSQL IOPS                | 348602.48  |
| CPU USage                | 58.22%     |

With only 59 nodes, YugabyteDB breezed through the 100K warehouse at a 99.83% efficiency and clocked 1.3 million transactions per minute.

## 150K warehouse

The following shows the results for the TPC-C benchmark for 150K warehouses.

| Cluster configuration |              |
| --------------------- | ------------ |
| YugabyteDB Version    | 2.11.0       |
| Instance Type         | c5d.12xlarge |
| Provider Type         | AWS          |
| Nodes                 | 75           |
| RF                    | 3            |
| Region                | us-west      |

| Results                  |           |
| ------------------------ | --------- |
| Efficiency               | 99.3      |
| TPMC                     | 1M        |
| Average NewOrder Latency | 123.33 ms |
| YSQL Ops/sec             | 950K      |
| CPU USage                | 80%       |

The latency and IOPS during the test execution are shown in the following illustration.

![Ops and Latency](/images/explore/scalability/150k_warehouse_latency.png)

## Learn more

- [YugabyteDB Performance Benchmarks](../../../benchmark/)