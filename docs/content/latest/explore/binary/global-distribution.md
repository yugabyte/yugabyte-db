## 1. Create a multi-zone universe in US West

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```

Start a new local universe with replication factor 3, and each replica placed in different zones (`us-west-2a`, `us-west-2b`, `us-west-2c`) in the `us-west-2` (Oregon) region of AWS. This can be done by running the following: 

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl create --placement_info "aws.us-west-2.us-west-2a,aws.us-west-2.us-west-2b,aws.us-west-2.us-west-2c"
```


In this deployment, the YB Masters are each placed in a separate zone to allow them to survive the loss of a zone. You can view the masters on the [dashboard](http://localhost:7000/).

![Multi-zone universe masters](/images/ce/online-reconfig-multi-zone-masters.png)

You can view the tablet servers on the [tablet servers page](http://localhost:7000/tablet-servers).

![Multi-zone universe tservers](/images/ce/online-reconfig-multi-zone-tservers.png)

## 2. Start a workload

Let us run a simple key-value workload in a separate shell.

```{.sh .copy .separator-dollar}
$ java -jar java/yb-sample-apps.jar \
    --workload CassandraKeyValue \
    --nodes 127.0.0.1:9042 \
    --num_threads_read 1 \
    --num_threads_write 1
```

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers).

![Multi-zone universe load](/images/ce/online-reconfig-multi-zone-load.png)

## 3. Add a region in US East

Add a node in the region `us-east-1`, zone `us-east-1a`. 

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl add_node --placement_info "aws.us-east-1.us-east-1a"
```

At this point, a new node is added into `us-east-1` region in zone `us-east-1a`, but it should not be taking any read or write IO. This is because the initial placement info in YB Master instructs it to continue its previous placement policy across the zones in `us-west-2` region.

![Add node in a new region](/images/ce/online-reconfig-add-region-no-load.png)

Let us now update the placement config, instructing the YB-Master to place data in the new region (`region2`).

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    modify_placement_info aws.us-west-2.us-west-2a,aws.us-west-2.us-west-2b,aws.us-east-1.us-east-1a 3
```

You should see that the data as well as the IO gradually moves from node #3 in the zone `us-west-2c` to the newly added node (node #4) in the zone `us-east-1a`. The [tablet servers page](http://localhost:7000/tablet-servers) should soon look something like the screenshot below.

![Multi region workload](/images/ce/online-reconfig-multi-region-load.png)

Next we need to move the YB-Master from node #3 to node #4. In order to do so, first start a new master process on node 4.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl add_node --master
```

Add node #4 into the master Raft group.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 change_master_config ADD_SERVER 127.0.0.4 7100
```

![Add master](/images/ce/online-reconfig-add-master.png)

Remove node #3 from the master Raft group.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100,127.0.0.4:7100 change_master_config REMOVE_SERVER 127.0.0.3 7100
```

![Add master](/images/ce/online-reconfig-remove-master.png)


## 4. Add a region in Tokyo


Let us now add another node in a the Tokyo zone `ap-northeast-1a`. You can do so by running the command below. 

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl add_node --placement_info "aws.ap-northeast-1.ap-northeast-1a"
```

You can see the node added in the screenshot below.

![Multi region universe](/images/ce/online-reconfig-add-region-no-load-2.png)

Next, update the placement config instructing the YB-Master to place data in the new cloud.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    modify_placement_info aws.us-west-2.us-west-2a,aws.us-east-1.us-east-1c,aws.ap-northeast-1.ap-northeast-1a 3
```

The new node should now start taking load gradually until it finally is serving all the load from node #2.

![Multi region universe with load](/images/ce/online-reconfig-multi-region-load-2.png)

As before, let us add node #5 into the master Raft group and remove node #2 from it. First start the master on node #5.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl add_node --master
```


You can add node #5 into the master Raft group by running the following commands.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.4:7100 change_master_config ADD_SERVER 127.0.0.5 7100
```

Remove node #3 from the master Raft group.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.4:7100,127.0.0.5:7100 change_master_config REMOVE_SERVER 127.0.0.2 7100
```

The final master Raft group should look as follows.

![Multi region universe with load](/images/ce/online-reconfig-change-master-2.png)


## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```
