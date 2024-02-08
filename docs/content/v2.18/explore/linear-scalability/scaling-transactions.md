---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Scaling concurrent transactions in YugabyteDB.
headcontent: Transaction performance when scaling out
menu:
  v2.18:
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

## TPC-C results

The following shows the results for the TPC-C benchmark using the following configuration:

- YugabyteDB version 2.18.0
- AWS, us-west region, c5d.9xlarge instances
- Replication factor 3

### 100K warehouse

The 100K benchmark was run on a universe with 59 nodes.

| Efficiency | TPMC       | Average NewOrder Latency  (ms) | YSQL Ops/sec | CPU USage (%) |
| :--------- | ---------- | ------------------------------ | ------------ | ------------- |
| 99.83      | 1283804.18 | 51.86                          | 348602.48    | 58.22         |

With only 59 nodes, YugabyteDB breezed through the 100K warehouse at a 99.83% efficiency and clocked 1.3 million transactions per minute.

### 150K warehouse

The 100K benchmark was run on a universe scaled out to 75 nodes.

| Efficiency | TPMC | Average NewOrder Latency  (ms) | YSQL Ops/sec | CPU USage (%) |
| :--------- | -----| ------------------------------ | ------------ | ------------- |
| 99.3       | 1M   | 123.33                         | 950K         | 80            |

The latency and IOPS during the test execution are shown in the following illustration.

![Ops and Latency](/images/explore/scalability/150k_warehouse_latency.png)

## Learn more

- [YugabyteDB Performance Benchmarks](../../../benchmark/)