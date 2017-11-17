---
date: 2016-03-09T00:11:02+01:00
title: Linear Scalability
weight: 23
---

With YugaByte, you can add nodes to scale your cluster up very efficiently and reliablly in order to achieve more read and write IOPS. In this tutorial, we will look at how YugaByte can scale while a workload is running. We will run a read-write workload using a pre-packaged sample application against a 3-node local cluster with a replication factor of 3, and add nodes to it while the workload is running. We will then observe how the cluster scales out, by verifying that the number of read/write IOPS are evenly distributed across all the nodes at all times.


## Step 1. Setup - create universe

If you have a previously running local universe, destroy it using the following:

```sh
./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```sh
./bin/yb-ctl create
```


## Step 2. Run sample key-value app

Run the CQL sample key-value app against the local universe by typing the following command.

```sh
java -jar ./java/yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes 127.0.0.1:9042 \
                                    --num_threads_write 1 \
                                    --num_threads_read 4 \
                                    --value_size 4096
```

The sample application prints some stats while running, which is also shown below. You can read more details about the output of the sample applications [here](/quick-start/run-sample-apps/).

```sh
2017-11-20 14:02:48,114 [INFO|...] Read: 9893.73 ops/sec (0.40 ms/op), 233458 total ops  |
                                   Write: 1155.83 ops/sec (0.86 ms/op), 28072 total ops  |  ...

2017-11-20 14:02:53,118 [INFO|...] Read: 9639.85 ops/sec (0.41 ms/op), 281696 total ops  |
                                   Write: 1078.74 ops/sec (0.93 ms/op), 33470 total ops  |  ...
```

## Step 3. Observe IOPS per node

You can check a lot of the per-node stats by browsing to the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total read and write IOPS per node are highlighted in the screenshot below. Note that both the reads and the writes are roughly the same across all the nodes indicating uniform usage across the nodes.

![Read and write IOPS with 3 nodes](/images/explore-core-features/linear-scalability-3-nodes.png)

## Step 4. Add node and observe linear scale out

Add a node to the universe.

```sh
./bin/yb-ctl add_node
```

Now we should have 4 nodes. Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. In a short time, you should see the new node performing a comparable number of reads and writes as the other nodes.

![Read and write IOPS with 4 nodes](/images/explore-core-features/linear-scalability-4-nodes.png)

## Step 5. Add another node and observe linear scale out

Add yet another node to the universe.

```sh
./bin/yb-ctl add_node
```

Now we should have 5 nodes. Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. In a short time, you should see the new node performing a comparable number of reads and writes as the other nodes.

![Read and write IOPS with 5 nodes](/images/explore-core-features/linear-scalability-5-nodes.png)

The YugaByte universe automatically let the client know to use the newly added nodes for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes. 

## Step 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```
