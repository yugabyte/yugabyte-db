## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ ./yb-docker-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 

```sh
$ ./yb-docker-ctl create --enable_postgres
```

## 2. Run sample key-value app

Download the sample app jar.

```sh
$ wget https://github.com/YugaByte/yb-sql-workshop/blob/master/running-sample-apps/yb-sample-apps.jar
```

Run the `SqlInserts` sample key-value app against the local universe by typing the following command.

```sh
$ java -jar ./yb-sample-apps.jar --workload SqlInserts \
                                    --nodes 127.0.0.1:5433 \
                                    --num_threads_write 1 \
                                    --num_threads_read 4
```

The sample application prints some stats while running, which is also shown below. You can read more details about the output of the sample applications [here](../../../quick-start/run-sample-apps/).

```sh
2018-05-10 09:10:19,538 [INFO|...] Read: 8988.22 ops/sec (0.44 ms/op), 818159 total ops  |  Write: 1095.77 ops/sec (0.91 ms/op), 97120 total ops  | ... 
2018-05-10 09:10:24,539 [INFO|...] Read: 9110.92 ops/sec (0.44 ms/op), 863720 total ops  |  Write: 1034.06 ops/sec (0.97 ms/op), 102291 total ops  | ...
```

## 3. Observe even load across all nodes

You can check a lot of the per-node stats by browsing to the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total read and write IOPS per node are highlighted in the screenshot below. Note that both the reads and the writes are roughly the same across all the nodes indicating uniform usage across the nodes.

![Read and write IOPS with 3 nodes](/images/ce/pgsql-fault-tolerance-3-nodes.png)

## 4. Remove a node and observe continuous write availability

Remove a node from the universe.

```sh
$ ./yb-docker-ctl remove_node 3
```

Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. The `Time since heartbeat` value for that node will keep increasing. Once that number reaches 60s (i.e. 1 minute), YugaByte DB will change the status of that node from ALIVE to DEAD. Note that at this time the universe is running in an under-replicated state for some subset of tablets.

![Read and write IOPS with 3rd node dead](/images/ce/pgsql-fault-tolerance-1-node-dead.png)


## 4. Remove another node and observe write unavailability

Remove another node from the universe.

```sh
$ ./yb-docker-ctl remove_node 2
```

Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. Writes are now unavailable but reads can continue to be served for whichever tablets available on the remaining node.

![Read and write IOPS with 2nd node removed](/images/ce/pgsql-fault-tolerance-2-nodes-dead.png)

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./yb-docker-ctl destroy
```
