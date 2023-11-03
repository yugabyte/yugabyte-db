---
title: Read replicas
headerTitle: Read replicas
linkTitle: Read replicas
description: Read replicas
menu:
  v2.12:
    name: Read replicas
    identifier: explore-multi-region-deployments-read-replicas-ycql
    parent: explore-multi-region-deployments
    weight: 750
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../read-replicas-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../read-replicas-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

YugabyteDB supports the following types of reads:

- [Follower reads](../../ysql-language-features/going-beyond-sql/follower-reads-ycql/) that enable spreading the read workload across all replicas in the primary cluster.
- Observer reads that use read replicas. Read replicas are created as a separate cluster that may be located in a different region, possibly closer to the consumers of the data which would result in lower-latency access and enhanced support of analytics workloads.

A datacenter (also known as universe) can have one primary cluster and several read replica clusters.

Stale reads are possible with an upper bound on the amount of staleness. Reads are guaranteed to be timeline-consistent. You need to set the consistency level to `ONE` in your application to work with follower reads or observer reads. In addition, you have to set the application’s local datacenter to the read replica cluster's region.

## Prerequisites

Ensure that you have downloaded and configured YugabyteDB, as described in [Quick Start](/preview/quick-start/).

{{< note title="Note" >}}

This document uses a client application based on the [yb-sample-apps](https://github.com/yugabyte/yb-sample-apps) workload generator.

{{< /note >}}

Also, since you cannot use read replicas without a primary cluster, ensure that you have the latter available. The following command sets up a primary cluster of three nodes in cloud `c`, region `r` and zones `z1`, `z2`, and `z3`:

```shell
$ ./bin/yb-ctl create --rf 3 --placement_info "c.r.z1,c.r.z2,c.r.z3" --tserver_flags "placement_uuid=live,max_stale_read_bound_time_ms=60000000”
```

Output:

```output
Creating cluster.
Waiting for cluster to be ready.
....
---------------------------------------------------------------
| Node Count: 3 | Replication Factor: 3                       |
---------------------------------------------------------------
| JDBC            : jdbc:postgresql://127.0.0.1:5433/yugabyte |
| YSQL Shell      : bin/ysqlsh                                |
| YCQL Shell      : bin/ycqlsh                                |
| YEDIS Shell     : bin/redis-cli                             |
| Web UI          : http://127.0.0.1:7000/                    |
| Cluster Data    : /Users/yourname/yugabyte-data             |
---------------------------------------------------------------

For more info, please use: yb-ctl status
```

The following command instructs the masters to create three replicas for each tablet distributed across the three zones:

```shell
$ ./bin/yb-admin -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 modify_placement_info c.r.z1,c.r.z2,c.r.z3 3 live
```

The following illustration demonstrates the primary cluster visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas1.png)

The following command runs the sample application and starts a YCQL workload:

```shell
java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                               --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \
                               --nouuid \
                               --num_unique_keys 2 \
                               --num_threads_write 1 \
                               --num_threads_read 1 \
                               --value_size 1024
```

Output:

```output
0 [main] INFO com.yugabyte.sample.Main  - Starting sample app...
35 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Using NO UUID
44 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - App: CassandraKeyValue
44 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Run time (seconds): -1
44 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Adding node: 127.0.0.1:9042
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Adding node: 127.0.0.2:9042
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Adding node: 127.0.0.3:9042
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num reader threads: 1, num writer threads: 1
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num unique keys to insert: 2
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num keys to update: 1999998
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num keys to read: 1500000
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Value size: 1024
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Restrict values to ASCII strings: false
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Perform sanity check at end of app run: false
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Table TTL (secs): -1
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Local reads: false
45 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Read only load: false
48 [main] INFO com.yugabyte.sample.apps.AppBase  - Creating Cassandra tables...
92 [main] INFO com.yugabyte.sample.apps.AppBase  - Connecting with 4 clients to nodes: /127.0.0.1:9042,/127.0.0.2:9042,/127.0.0.3:9042
1139 [main] INFO com.yugabyte.sample.apps.AppBase  - Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));]
6165 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker - Read: 4009.75 ops/sec (0.24 ms/op), 20137 total ops | Write: 1650.50 ops/sec (0.60 ms/op), 8260 total ops | Uptime: 5025 ms | maxWrittenKey: 1 | maxGeneratedKey: 2 |
11166 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker - Read: 5066.60 ops/sec (0.20 ms/op), 45479 total ops | Write: 1731.19 ops/sec (0.58 ms/op), 16918 total ops | Uptime: 10026 ms | maxWrittenKey: 1 | maxGeneratedKey: 2 |
```

The following illustration demonstrates the read and write statistics in the primary cluster visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas2.png)

As per the preceding illustration, using the default workload directs reads and writes to the tablet leader. The arguments in the `java -jar ./yb-sample-apps.jar` command explicitly restrict the number of keys to be written and read to one in order to follow the reads and writes occurring on a single tablet.

The following is a modified command that enables follower reads. Specifying `--local_reads` changes the consistency level to `ONE`. The `--with_local_dc` option defines in which datacenter the application is at any given time. When specified, the read traffic is routed to the same region:

```shell
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                 --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \
                                 --nouuid \
                                 --num_unique_keys 2 \
                                 --num_threads_write 1 \
                                 --num_threads_read 1 \
                                 --value_size 1024 --local_reads --with_local_dc r
```

Output:

```output
0 [main] INFO com.yugabyte.sample.Main - Starting sample app...
22 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Using NO UUID
27 [main] INFO com.yugabyte.sample.common.CmdLineOpts - App: CassandraKeyValue
27 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Run time (seconds): -1
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Adding node: 127.0.0.1:9042
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Adding node: 127.0.0.2:9042
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Adding node: 127.0.0.3:9042
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Num reader threads: 1, num writer threads: 1
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Num unique keys to insert: 2
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Num keys to update: 1999998
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Num keys to read: 1500000
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Value size: 1024
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Restrict values to ASCII strings: false
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Perform sanity check at end of app run: false
28 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Table TTL (secs): -1
29 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Local reads: true
29 [main] INFO com.yugabyte.sample.common.CmdLineOpts - Read only load: false
30 [main] INFO com.yugabyte.sample.apps.AppBase - Creating Cassandra tables...
67 [main] INFO com.yugabyte.sample.apps.AppBase - Connecting with 4 clients to nodes: /127.0.0.1:9042,/127.0.0.2:9042,/127.0.0.3:9042
751 [main] INFO com.yugabyte.sample.apps.AppBase - Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));]
5773 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker - Read: 4029.25 ops/sec (0.24 ms/op), 20221 total ops | Write: 1486.29 ops/sec (0.67 ms/op), 7440 total ops | Uptime: 5021 ms | maxWrittenKey: 1 | maxGeneratedKey: 2 |
10778 [Thread-1] INFO com.yugabyte.sample.common.metrics.MetricsTracker - Read: 4801.54 ops/sec (0.21 ms/op), 44256 total ops | Write: 1637.30 ops/sec (0.61 ms/op), 15635 total ops | Uptime: 10026 ms | maxWrittenKey: 1 | maxGeneratedKey: 2 |
```

The following illustration demonstrates the reads spread across all the replicas for the tablet visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas3.png)

## Using Read Replicas

The following commands add three new nodes to a read replica cluster in region `r2`:

```shell
./bin/yb-ctl add_node --placement_info "c.r2.z21" --tserver_flags "placement_uuid=rr"
./bin/yb-ctl add_node --placement_info "c.r2.z22" --tserver_flags "placement_uuid=rr"
./bin/yb-ctl add_node --placement_info "c.r2.z23" --tserver_flags "placement_uuid=rr"

./bin/yb-admin -master_addresses 127.0.0.1:7100,127.0.0.2,127.0.0.3 add_read_replica_placement_info c.r2.z21:1,c.r2.z22:1,c.r2.z23:1 3 rr
```

The following illustration demonstrates the setup of two clusters, one of which is primary and another one is read replica visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas4.png)

The following command directs `CL.ONE` reads to the primary cluster (as follower reads) in region `r`:

```shell
java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                               --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \
                               --nouuid \
                               --num_unique_keys 2 \
                               --num_threads_write 1 \
                               --num_threads_read 1 \
                               --value_size 1024 --local_reads --with_local_dc r
```

The following illustration demonstrates the result of exectuting the preceding command (visible via [Yugabyte Platform](/preview/yugabyte-platform/):):

![img](/images/explore/multi-region-deployments/read-replicas5.png)

The following command directs the `CL.ONE` reads to the read replica cluster (as observer reads) in region `r2`:

```shell
java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                               --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \
                               --nouuid \
                               --num_unique_keys 2 \
                               --num_threads_write 1 \
                               --num_threads_read 1 \
                               --value_size 1024 --local_reads --with_local_dc r2
```

The following illustration demonstrates the result of exectuting the preceding command (visible via [Yugabyte Platform](/preview/yugabyte-platform/)):

![img](/images/explore/multi-region-deployments/read-replicas6.png)

For information on deploying read replicas, see [Read Replica Clusters](../../../deploy/multi-dc/read-replica-clusters/).

## Fault Tolerance

In the strong consistency mode (default), more failures can be tolerated by increasing the number of replicas: to tolerate a `k` number of failures, `2k+1` replicas are required in the RAFT group. However, follower reads and observer reads can provide Cassandra-style `CL.ONE` fault tolerance. The  `max_stale_read_bound_time_ms` GFlag controls how far behind the followers are allowed to be before they redirect reads back to the RAFT leader (the default is 60 seconds). For "write once, read many times” workloads, this number could be increased. By stopping nodes, you can induce behavior of follower and observer reads such that they continue to read (which would not be possible without follower reads).

The following command starts a read-only workload:

```shell
java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
  --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 \
  --nouuid \
  --max_written_key 2 \
  --read_only \
  --num_threads_read 1 \
  --value_size 1024 --local_reads --with_local_dc r2 --skip_ddl
```

The following command stops a node in the read replica cluster:

```shell
./bin/yb-ctl stop_node 6
```

The following illustration demonstrates the stopped node visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas7.png)

Stopping one node redistributes the load onto the two remaining nodes.

The following command stops another node in the read replica cluster:

```shell
./bin/yb-ctl stop_node 5
```

The following illustration demonstrates two stopped nodes visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas8.png)

The following command stops the last node in the read replica cluster which causes the reads revert back to the primary cluster and become follower reads:

```shell
./bin/yb-ctl stop_node 4
```

The following illustration demonstrates all stopped nodes in the read replica cluster and activation of nodes in the primary cluster visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas9.png)

This behavior differs from the standard Cassandra behavior. The YCQL interface only honors consistency level `ONE`. All other consistency levels are converted to `QUORUM` including `LOCAL_ONE`. When a local datacenter is specified by the application with consistency level `ONE`, read traffic is localized to the region as long as this region has active replicas. If the application’s local datacenter has no replicas, the read traffic is routed to the primary region.

The following command stops one of the nodes in the primary cluster:

```shell
./bin/yb-ctl stop_node 3
```

The following illustration demonstrates the state of nodes, with read load rebalanced to the remaining nodes visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas10.png)

The following command stops one more node in the primary cluster:

```shell
./bin/yb-ctl stop_node 2
```

The following illustration demonstrates that the entire read load moved to the one remaining node visible via [Yugabyte Platform](/preview/yugabyte-platform/):

![img](/images/explore/multi-region-deployments/read-replicas11.png)

For additional information, see [Fault Tolerance](../../fault-tolerance/macos/).
