---
date: 2016-03-09T00:11:02+01:00
title: Fault Tolerance
weight: 21
---

This section uses the binary version of the local cluster.

## Step 1. Create local cluster 

Start a new local cluster on terminal 1.

```sh
# destroy any previously running local cluster
$ ./bin/yb-ctl destroy

# create a new local cluster
$ ./bin/yb-ctl create
```

## Step 2. Run sample app 

Start the CQL sample app on terminal 2. Observe the output with 3 `yb-tserver` nodes.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 --num_threads_write 1
```

## Step 3. Remove a node

Remove one node to bring the cluster to only 2 `yb-tserver` nodes.

```sh
# check status and get node_ids
$ ./bin/yb-ctl status
2017-10-23 16:44:25,855 INFO: Server is running: type=master, node_id=1, PID=3097, admin service=127.0.0.1:7000
2017-10-23 16:44:26,843 INFO: Server is running: type=master, node_id=2, PID=3100, admin service=127.0.0.2:7000
2017-10-23 16:44:27,120 INFO: Server is running: type=master, node_id=3, PID=3103, admin service=127.0.0.3:7000
2017-10-23 16:44:27,652 INFO: Server is running: type=tserver, node_id=1, PID=3106, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-23 16:44:27,986 INFO: Server is running: type=tserver, node_id=2, PID=3109, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-23 16:44:28,058 INFO: Server is running: type=tserver, node_id=3, PID=3112, admin service=127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379

# remove node 1 from local cluster
$ ./bin/yb-ctl remove_node 3
2017-10-23 17:10:55,576 INFO: Removing server type=tserver node_id=3
2017-10-23 17:10:56,803 INFO: Stopping server type=tserver node_id=3 PID=3112
2017-10-23 17:10:56,803 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...
2017-10-23 17:10:57,304 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...
2017-10-23 17:10:57,808 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...
2017-10-23 17:10:58,309 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...
2017-10-23 17:10:58,810 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...
2017-10-23 17:10:59,315 INFO: Waiting for server type=tserver node_id=3 PID=3112 to stop...

# check status again
$ ./bin/yb-ctl status
2017-10-23 17:11:39,438 INFO: Server is running: type=master, node_id=1, PID=3097, admin service=127.0.0.1:7000
2017-10-23 17:11:39,587 INFO: Server is running: type=master, node_id=2, PID=3100, admin service=127.0.0.2:7000
2017-10-23 17:11:39,962 INFO: Server is running: type=master, node_id=3, PID=3103, admin service=127.0.0.3:7000
2017-10-23 17:11:40,298 INFO: Server is running: type=tserver, node_id=1, PID=3106, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-23 17:11:40,630 INFO: Server is running: type=tserver, node_id=2, PID=3109, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-23 17:11:40,970 INFO: Server type=tserver node_id=3 is not running
```

## Step 4. Observe sample app

Observe sample app output again to see a temporary connectivity issue to the node that was just killed. The client immediately fails over to a live node and then overall throughput/latency remains unchanged. This is because even with 2 live nodes, YugaByte can continue to update majority of replicas (i.e. 2 out of 3 replicas) and hence can continue to process the writes from the CQL sample app.

If you remove one more node then you can see that write operations from the CQL sample app no longer succeed. This is expected since the number of alive nodes (i.e. 1) is less than the number required to establish majority (i.e. 2 nodes out of 3).