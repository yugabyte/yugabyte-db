## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ ./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. We configure the number of [shards](../../../architecture/concepts/sharding/) (aka tablets) per table per tserver to 4 so that we can better observe the load balancing during scale-up and scale-down. Each table will now have 4 tablet-leaders in each tserver and with replication factor 3, there will be 2 tablet-followers for each tablet-leader distributed in the 2 other tservers. So each tserver will have 12 tablets (i.e. sum of 4 tablet-leaders and 8 tablet-followers) per table.

```sh
$ ./bin/yb-ctl --num_shards_per_tserver 4 create --enable_postgres
```

## 2. Run sample key-value app

Download the sample app jar.

```sh
$ wget https://github.com/YugaByte/yb-sql-workshop/blob/master/running-sample-apps/yb-sample-apps.jar?raw=true -O yb-sample-apps.jar
```

Run the `SqlInserts` sample key-value app against the local universe by typing the following command.

```sh
$ java -jar ./yb-sample-apps.jar --workload SqlInserts \
                                    --nodes 127.0.0.1:5433 \
                                    --num_threads_write 1 \
                                    --num_threads_read 4
```

The sample application prints some stats while running, which is also shown below. You can read more details about the output of the sample applications [here](../../../quick-start/run-sample-apps/).

```
2018-05-10 09:10:19,538 [INFO|...] Read: 8988.22 ops/sec (0.44 ms/op), 818159 total ops  |  Write: 1095.77 ops/sec (0.91 ms/op), 97120 total ops  | ... 
2018-05-10 09:10:24,539 [INFO|...] Read: 9110.92 ops/sec (0.44 ms/op), 863720 total ops  |  Write: 1034.06 ops/sec (0.97 ms/op), 102291 total ops  | ...
```

## 3. Observe IOPS per node

You can check a lot of the per-node stats by browsing to the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total read and write IOPS per node are highlighted in the screenshot below. Note that both the reads and the writes are roughly the same across all the nodes indicating uniform usage across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/linear-scalability-3-nodes.png)

## 4. Add node and observe linear scale out

Add a node to the universe.

```sh
$ ./bin/yb-ctl --num_shards_per_tserver 4 add_node
```

Now we should have 4 nodes. Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. In a short time, you should see the new node performing a comparable number of reads and writes as the other nodes. The 36 tablets will now get distributed evenly across all the 4 nodes, leading to each node having 9 tablets.

The YugaByte DB universe automatically let the client know to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes. 

![Read and write IOPS with 4 nodes - Rebalancing in progress](/images/ce/linear-scalability-4-nodes.png)

![Read and write IOPS with 4 nodes](/images/ce/linear-scalability-4-nodes-balanced.png)

## 5. Remove node and observe linear scale in

Remove the recently added node from the universe.

```sh
$ ./bin/yb-ctl remove_node 4
```

- Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. The `Time since heartbeat` value for that node will keep increasing. Once that number reaches 60s (i.e. 1 minute), YugaByte DB will change the status of that node from ALIVE to DEAD. Note that at this time the universe is running in an under-replicated state for some subset of tablets.

![Read and write IOPS with 4th node dead](/images/ce/linear-scalability-4-nodes-dead.png)

- After 300s (i.e. 5 minutes), YugaByte DB's remaining nodes will re-spawn new tablets that were lost with the loss of node 4. Each remaining node's tablet count will increase from 9 to 12, thus getting back to the original state of 36 total tablets.

![Read and write IOPS with 4th node removed](/images/ce/linear-scalability-3-nodes-rebalanced.png)

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```
