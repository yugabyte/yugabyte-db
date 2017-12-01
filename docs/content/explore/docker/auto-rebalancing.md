## Step 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
./yb-docker-ctl destroy
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```sh
./yb-docker-ctl create
```


## Step 2. Run sample key-value app

Run the CQL sample key-value app against the local universe by typing the following command.

```sh
java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes localhost:9042 \
                                    --num_threads_write 1 \
                                    --num_threads_read 4 \
                                    --value_size 4096
```

## Step 3. Observe data sizes per node

You can check a lot of the per-node stats by browsing to the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total data size per node as well as the total memory used per node are highlighted in the screenshot below. Note that both of those metrics are roughly the same across all the nodes indicating uniform usage across the nodes.

![Tablet count, data and memory sizes with 3 nodes](/images/ce/auto-rebalancing-3-nodes-docker.png)

## Step 4. Add a node and observe data rebalancing

Add a node to the universe.

```sh
./yb-docker-ctl add_node
```

Now we should have 4 nodes. Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. As you refresh, you should see the new node getting more and more tablets, which would cause it to get more data as well as increase its memory footprint. Finally, all the 4 nodes should end up with a similar data distribution and memory usage.

![Tablet count, data and memory sizes with 4 nodes](/images/ce/auto-rebalancing-4-nodes-docker.png)

## Step 5. Add another node and observe linear scale out

Add yet another node to the universe.

```sh
./yb-docker-ctl add_node
```

Now we should have 5 nodes. Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update, and as before you should see all the nodes end up with similar data sizes and memory footprints.

![Tablet count, data and memory sizes with 5 nodes](/images/ce/auto-rebalancing-5-nodes-docker.png)

The YugaByte universe automatically balances its tablet-peer leaders by moving them in a throttled fashion into the newly added nodes. This automatic balancing of the data is completely transparent to the application logic.

## Step 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./yb-docker-ctl destroy
```
