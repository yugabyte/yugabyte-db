## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```
Start a new local cluster. By default, this will create a 3-node universe with a replication factor of 3. We configure the number of [shards](/architecture/concepts/sharding/)(aka tablets) per table per tserver to 4 so that we can better observe the load balancing during scale-up and scale-down. Each table will now have 4 tablet-leaders in each tserver and with replication factor 3, there will be 2 tablet-followers for each tablet-leader distributed in the 2 other tservers. So each tserver will have 12 tablets (i.e. sum of 4 tablet-leaders and 8 tablet-followers) per table. 


```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create --num_shards_per_tserver 4
```

## 2. Run sample key-value app

Run the Cassandra sample key-value app against the local universe by typing the following command.

```{.sh .copy .separator-dollar}
$ docker cp yb-master-n1:/home/yugabyte/java/yb-sample-apps.jar .
```
```{.sh .copy .separator-dollar}
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes localhost:9042 \
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

## 3. Observe IOPS per node

You can check a lot of the per-node stats by browsing to the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total read and write IOPS per node are highlighted in the screenshot below. Note that both the reads and the writes are roughly the same across all the nodes indicating uniform usage across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/linear-scalability-3-nodes-docker.png)

## 4. Add node and observe linear scale out

Add a node to the universe.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl add_node --num_shards_per_tserver 4
```

Now we should have 4 nodes. Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. In a short time, you should see the new node performing a comparable number of reads and writes as the other nodes.

![Read and write IOPS with 4 nodes - Rebalancing in progress](/images/ce/linear-scalability-4-nodes-docker.png)

![Read and write IOPS with 4 nodes - Balanced](/images/ce/linear-scalability-4-nodes-balanced-docker.png)

## 5. Remove node and observe linear scale in

Remove the recently added node from the universe.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl remove_node 4
```

- Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. The `Time since heartbeat` value for that node will keep increasing. Once that number reaches 60s (i.e. 1 minute), YugaByte DB will change the status of that node from ALIVE to DEAD. Note that at this time the universe is running in an under-replicated state for some subset of tablets.

![Read and write IOPS with 4th node dead](/images/ce/linear-scalability-4-nodes-dead-docker.png)

- After 300s (i.e. 5 minutes), YugaByte DB's remaining nodes will re-spawn new tablets that were lost with the loss of node 4. Each remaining node's tablet count will increase from 18 to 24.


## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```
