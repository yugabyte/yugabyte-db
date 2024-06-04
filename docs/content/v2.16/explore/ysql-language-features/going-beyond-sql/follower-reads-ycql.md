---
title: Explore follower reads in YCQL
headerTitle: Follower reads
linkTitle: Follower reads
description: Learn how you can use follower reads to lower read latencies in local YugabyteDB clusters.
menu:
  v2.16:
    identifier: follower-reads-ycql
    parent: going-beyond-sql
    weight: 285
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../follower-reads-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>YSQL</a>
  </li>

  <li >
    <a href="../follower-reads-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>YCQL</a>
  </li>

</ul>

Use follower reads to lower read latencies. With follower reads, the database has less work to do at read time, including serving the read from the tablet followers. Follower reads is similar to reading from a cache, which can give more read IOPS with low latency, but might have slightly stale yet timeline-consistent data (that is, no out of order data is possible).

## Considerations

Consider the following factors when using follower reads in YCQL.

### Consistency level

You need to set the consistency level to `ONE` in your application to work with follower reads or observer reads. Changing the consistency level to `ONE` has no effect on write operations, only read operations. This is because writes in YCQL are always strongly consistent. Note that the default consistency level is `QUORUM`.

Using [ycqlsh](../../../../admin/ycqlsh), you can check the consistency level using the `CONSISTENCY` command with no arguments, and set the consistency level to one using `CONSISTENCY ONE`. To learn about the consistency levels in YCQL, refer to [CONSISTENCY](../../../../admin/ycqlsh/#consistency).

### Maximum staleness

You can specify the maximum staleness of data when reading from tablet followers. If the follower hasn't heard from the leader for 10 seconds (the default), the read request is forwarded to the leader. If the tablet follower and the tablet leader are far from each other, you might need to increase the duration. To change the duration for maximum staleness, add the [`yb-tserver` `--max_stale_read_bound_time_ms`](../../../../reference/configuration/yb-tserver/#max-stale-read-bound-time-ms) flag and increase the value. For information on adding this flag when creating a cluster using `yb-ctl`, refer to [Creating a local cluster with custom flags](../../../../admin/yb-ctl/#create-a-local-cluster-with-custom-flags).

## Try it out

In this tutorial, you update a single key-value over and over, and read it from the tablet leader. While that workload is running, you start another workload to read from a follower and verify that you are able to read from a tablet follower.

### Create a cluster

If you have a previously running cluster, destroy it by executing the following command:

```sh
$ ./bin/yb-ctl destroy
```

Start a new local cluster with three nodes and a replication factor (RF) of `3`, as follows:

```sh
$ ./bin/yb-ctl --rf 3 create
```

Add one more node, as follows:

```sh
$ ./bin/yb-ctl add_node
```

### Write some data

Download the [YugabyteDB workload generator](https://github.com/yugabyte/yb-sample-apps) JAR file (`yb-sample-apps.jar`) by running the following command:

```sh
wget 'https://github.com/yugabyte/yb-sample-apps/releases/download/1.3.9/yb-sample-apps.jar?raw=true' -O yb-sample-apps.jar
```

By default, the YugabyteDB workload generator runs with strong read consistency, where all data is read from the tablet leader. Note that the `yb-sample-apps.jar` sets the [consistency](../../../../admin/ycqlsh/#consistency) level to ONE by default. You can populate exactly one key with a `10KB` value into the system. Because the replication factor is `3`, this key is replicated to only three of the four nodes in the cluster.

Run the `CassandraKeyValue` workload application to constantly update this key-value, as well as perform reads with strong consistency against the local cluster, as follows:

```sh
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes 127.0.0.1:9042 \
                                    --nouuid \
                                    --num_unique_keys 1 \
                                    --num_threads_write 1 \
                                    --num_threads_read 1 \
                                    --value_size 10240
```

In the preceding command, the value of `num_unique_keys` is set to `1`, which means a single key `key:0` was overwritten. You can verify this using ycqlsh as follows:

```sh
$ ./bin/ycqlsh 127.0.0.1
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
```

Run a query as follows:

```sql
ycqlsh> SELECT k FROM ybdemo_keyspace.cassandrakeyvalue;
```

```output
 k
-------
 key:0

(1 rows)
```

### Strongly consistent reads from tablet leaders

When performing strongly consistent reads as a part of the above command, all reads are served by the tablet leader of the tablet that contains the key `key:0`. Browse to the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page, where you can see that all the requests are indeed being served by one YB-TServer, as shown in the following illustration:

![Reads from the tablet leader](/images/ce/tunable-reads-leader.png)

### Follower reads from tablet replicas

Stop the workload application above, and then run the following variant of that workload application. This command performs updates to the same key `key:0` via the tablet leader, but it reads from the replicas, as follows:

```sh
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes 127.0.0.1:9042 \
                                    --nouuid \
                                    --num_unique_keys 1 \
                                    --num_threads_write 1 \
                                    --num_threads_read 1 \
                                    --value_size 10240 \
                                    --local_reads
```

You can see this by refreshing the [tablet-servers](http://127.0.0.1:7000/tablet-servers) page. The writes are served by a single YB-TServer that is the leader of the tablet for the key `key:0`, while multiple YB-TServer replicas serve the reads.

![Reads from the tablet follower](/images/ce/tunable-reads-followers.png)

### Clean up (optional)

Optionally, you can execute the following command to shut down the local cluster.

```sh
$ ./bin/yb-ctl destroy
```

## Read more

- [Read replica deployment](../../../../deploy/multi-dc/read-replica-clusters/)
- [Read replicas](../../../multi-region-deployments/read-replicas-ycql/)
