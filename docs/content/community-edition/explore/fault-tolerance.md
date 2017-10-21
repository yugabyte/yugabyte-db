---
date: 2016-03-09T00:11:02+01:00
title: Fault Tolerance
weight: 8
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
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.1:9043,127.0.0.1:9044
```

## Step 3. Remove a node

Remove one node to bring the cluster to only 2 `yb-tserver` nodes.

```sh
# check status and get node_ids
$ ./bin/yb-ctl status
2017-09-25 22:07:07,655 INFO: Server is running: type=master, node_id=1, PID=11140, URL=127.0.0.1:7000
2017-09-25 22:07:08,596 INFO: Server is running: type=master, node_id=2, PID=11143, URL=127.0.0.1:7001
2017-09-25 22:07:11,450 INFO: Server is running: type=master, node_id=3, PID=11146, URL=127.0.0.1:7002
2017-09-25 22:07:12,328 INFO: Server is running: type=tserver, node_id=1, PID=11149, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-25 22:07:12,811 INFO: Server is running: type=tserver, node_id=2, PID=11152, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-25 22:07:13,120 INFO: Server is running: type=tserver, node_id=3, PID=11155, URL=127.0.0.1:9002, cql port=9044, redis port=6381

# remove node 1 from local cluster
$ ./bin/yb-ctl remove_node 1
2017-09-25 22:07:28,570 INFO: Removing server type=tserver node_id=1
2017-09-25 22:07:29,198 INFO: Stopping server type=tserver node_id=1 PID=11149
2017-09-25 22:07:29,212 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...
2017-09-25 22:07:30,007 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...
2017-09-25 22:07:30,653 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...

# check status again
$ ./bin/yb-ctl status
2017-09-25 22:07:40,387 INFO: Server is running: type=master, node_id=1, PID=11140, URL=127.0.0.1:7000
2017-09-25 22:07:40,905 INFO: Server is running: type=master, node_id=2, PID=11143, URL=127.0.0.1:7001
2017-09-25 22:07:41,398 INFO: Server is running: type=master, node_id=3, PID=11146, URL=127.0.0.1:7002
2017-09-25 22:07:41,823 INFO: Server type=tserver node_id=1 is not running
2017-09-25 22:07:41,856 INFO: Server is running: type=tserver, node_id=2, PID=11152, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-25 22:07:41,862 INFO: Server is running: type=tserver, node_id=3, PID=11155, URL=127.0.0.1:9002, cql port=9044, redis port=6381
```

## Step 4. Observe sample app

Observe sample app output again to see a temporary connectivity issue to the node that was just killed. The client immediately fails over to a live node and then overall throughput/latency remains unchanged. This is because even with 2 live nodes, YugaByte can continue to update majority of replicas (i.e. 2 out of 3 replicas) and hence can continue to process the writes from the CQL sample app.

If you remove one more node then you can see that write operations from the CQL sample app no longer succeed. This is expected since the number of alive nodes (i.e. 1) is less than the number required to establish majority (i.e. 2 nodes out of 3).