---
title: Explore fault tolerance
headerTitle: Fault tolerance
linkTitle: Fault tolerance
description: Simulate fault tolerance and resilience in a local three-node YugabyteDB database cluster.
menu:
  v2.14:
    identifier: fault-tolerance-1-macos
    parent: explore
    weight: 215
type: docs
---

YugabyteDB can automatically handle failures and therefore provides [high availability](../../../architecture/core-functions/high-availability/). This tutorial demonstrates how YugabyteDB can continue to do reads and writes even in case of node failures. You create YSQL tables with a replication factor (RF) of 3, which allows a [fault tolerance](../../../architecture/docdb-replication/replication/) of 1. This means the cluster remains available for both reads and writes even if one node fails. However, if another node fails (bringing the number of failures to two), writes become unavailable on the cluster to preserve data consistency.

This tutorial uses the [yugabyted](../../../reference/configuration/yugabyted/) cluster management utility.

## Create a universe

To start a local three-node cluster with a replication factor of `3`, first create a single-node cluster as follows:

```sh
./bin/yugabyted start \
                --listen=127.0.0.1 \
                --base_dir=/tmp/ybd1
```

Next, join two more nodes with the previous node. By default, [yugabyted](../../../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` on starting a 3 node cluster.

```sh
./bin/yugabyted start \
                --listen=127.0.0.2 \
                --base_dir=/tmp/ybd2 \
                --join=127.0.0.1
```

```sh
./bin/yugabyted start \
                --listen=127.0.0.3 \
                --base_dir=/tmp/ybd3 \
                --join=127.0.0.1
```

## Run the sample key-value app

Download the YugabyteDB workload generator JAR file (`yb-sample-apps.jar`) using the following command:

```sh
wget https://github.com/yugabyte/yb-sample-apps/releases/download/1.3.9/yb-sample-apps.jar?raw=true -O yb-sample-apps.jar
```

Run the `SqlInserts` workload against the local universe using the following command:

```sh
java -jar ./yb-sample-apps.jar --workload SqlInserts \
                               --nodes 127.0.0.1:5433 \
                               --num_threads_write 1 \
                               --num_threads_read 4
```

The `SqlInserts` workload prints some statistics while running as follows:

```output
32001 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker  - Read: 4508.59 ops/sec (0.88 ms/op), 121328 total ops  |  Write: 658.11 ops/sec (1.51 ms/op), 18154 total ops  |  Uptime: 30024 ms | ...
37006 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker  - Read: 4342.41 ops/sec (0.92 ms/op), 143061 total ops  |  Write: 635.59 ops/sec (1.58 ms/op), 21335 total ops  |  Uptime: 35029 ms | ...
```

For more information about the output of the workload applications, refer to [YugabyteDB workload generator](https://github.com/yugabyte/yb-sample-apps).

## Observe even load across all nodes

You can check a lot of the per-node statistics by browsing to [tablet-servers](http://127.0.0.1:7000/tablet-servers). The total read and write IOPS per node are demonstrated by the following illustration:

![Read and write IOPS with 3 nodes](/images/ce/fault-tolerance_evenly_distributed.png)

Note how both the reads and the writes are roughly the same across all the nodes, indicating uniform usage across the nodes.

## Remove a node and observe continuous write availability

Remove a node from the universe using the following command:

```sh
./bin/yugabyted stop \
                  --base_dir=/tmp/ybd3
```

Refresh the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page to see the status update.

The `Time since heartbeat` value for that node will keep increasing. After that number reaches 60s (1 minute), YugabyteDB changes the status of that node from `ALIVE` to `DEAD`. Note that at this time the universe is running in an under-replicated state for some subset of tablets.

![Read and write IOPS with 3rd node dead](/images/ce/fault_tolerance_dead_node.png)

## Clean up

Optionally, you can shut down the local cluster you created as follows:

```sh
./bin/yugabyted destroy \
                --base_dir=/tmp/ybd1

./bin/yugabyted destroy \
                --base_dir=/tmp/ybd2

./bin/yugabyted destroy \
                --base_dir=/tmp/ybd3
```
