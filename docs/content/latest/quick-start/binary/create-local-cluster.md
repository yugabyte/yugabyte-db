## 1. Create a 3 node cluster with replication factor 3 

We will use the [`yb-ctl`](../../admin/yb-ctl/) utility located in the `bin` directory of the YugaByte DB package to create and administer a local cluster. The default data directory used is `/tmp/yugabyte-local-cluster`. You can change this directory with the `--data_dir` option. Detailed output for the *create* command is available in [yb-ctl Reference](../../admin/yb-ctl/#create-cluster).

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl create
```

You can now check `/tmp/yugabyte-local-cluster` to see `node-i` directories created where `i` represents the `node_id` of the node. Inside each such directory, there will be 2 disks `disk1` and `disk2` to highlight the fact that YugaByte DB can work with multiple disks at the same time. Note that the IP address of `node-i` is by default set to `127.0.0.i`.

## 2. Check cluster status with yb-ctl

Run the command below to see that we now have 3 `yb-master` processes and 3 `yb-tserver` processes running on this localhost. Roles played by these processes in a YugaByte cluster (aka Universe) is explained in detail [here](../../architecture/concepts/universe/).

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl status
```
```sh
2017-10-16 22:19:52,363 INFO: Server is running: type=master, node_id=1, PID=31926, admin service=127.0.0.1:7000
2017-10-16 22:19:52,438 INFO: Server is running: type=master, node_id=2, PID=31929, admin service=127.0.0.2:7000
2017-10-16 22:19:52,448 INFO: Server is running: type=master, node_id=3, PID=31932, admin service=127.0.0.3:7000
2017-10-16 22:19:52,462 INFO: Server is running: type=tserver, node_id=1, PID=31935, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-16 22:19:52,795 INFO: Server is running: type=tserver, node_id=2, PID=31938, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-16 22:19:53,476 INFO: Server is running: type=tserver, node_id=3, PID=31941, admin service=127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
```

## 3. Check cluster status with Admin UI

Node 1's [master Admin UI](../../admin/yb-master/#admin-ui) is available at http://127.0.0.1:7000 and the [tserver Admin UI](../../admin/yb-tserver/#admin-ui) is available at http://127.0.0.1:9000. You can visit the other nodes' Admin UIs by using their corresponding IP addresses.

### 3.1 Overview and Master status

Node 1's master Admin UI home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 3 and `Num Nodes (TServers)` as 3. The `Num User Tables` is 0 since there are no user tables created yet. YugaByte DB version number is also shown for your reference. 

![master-home](/images/admin/master-home-binary.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement. 

### 3.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 3 tservers along with the time since they last connected to this master via their regular heartbeats. Since there are no user tables created yet, we can see that the `Load (Num Tablets)` is 0 across all the 3 tservers. As new tables get added, new tablets (aka shards) will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary.png)

