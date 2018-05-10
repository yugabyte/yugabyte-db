## 1. Create a 3 node cluster with replication factor 3 

We will use the [`yb-docker-ctl`](../../admin/yb-docker-ctl/) utility downloaded in the previous step to create and administer a containerized local cluster. Detailed output for the *create* command is available in [yb-docker-ctl Reference](../../admin/yb-docker-ctl/#create-cluster).

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create
```
Clients can now connect to YugaByte DB's Cassandra-compatible YCQL API at `localhost:9042` and to the Redis-compatible YEDIS API at  `localhost:6379`.

## 2. Check cluster status with yb-docker-ctl

Run the command below to see that we now have 3 `yb-master` (yb-master-n1,yb-master-n2,yb-master-n3) and 3 `yb-tserver` (yb-tserver-n1,yb-tserver-n2,yb-tserver-n3) containers running on this localhost. Roles played by these containers in a YugaByte cluster (aka Universe) is explained in detail [here](../../architecture/concepts/universe/).

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl status
```
```sh
PID        Type       Node       URL                       Status          Started At          
26132      tserver    n3         http://172.18.0.7:9000    Running         2017-10-20T17:54:54.99459154Z
25965      tserver    n2         http://172.18.0.6:9000    Running         2017-10-20T17:54:54.412377451Z
25846      tserver    n1         http://172.18.0.5:9000    Running         2017-10-20T17:54:53.806993683Z
25660      master     n3         http://172.18.0.4:7000    Running         2017-10-20T17:54:53.197652566Z
25549      master     n2         http://172.18.0.3:7000    Running         2017-10-20T17:54:52.640188158Z
25438      master     n1         http://172.18.0.2:7000    Running         2017-10-20T17:54:52.084772289Z
```

## 3. Check cluster status with Admin UI

The [yb-master-n1 Admin UI](../../admin/yb-master/#admin-ui) is available at http://localhost:7000 and the [yb-tserver-n1 Admin UI](../../admin/yb-tserver/#admin-ui) is available at http://localhost:9000. Other masters and tservers do not have their admin ports mapped to localhost to avoid port conflicts. 

**NOTE**: Clients connecting to the cluster will connect to only yb-tserver-n1. In case of Docker for Mac, routing [traffic directly to containers](https://docs.docker.com/docker-for-mac/networking/#known-limitations-use-cases-and-workarounds) is not even possible today. Since only 1 node will receive the incoming client traffic, throughput expected for Docker-based local clusters can be significantly lower than binary-based local clusters.

### 3.1 Overview and Master status

The yb-master-n1 home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 3 and `Num Nodes (TServers)` as 3. The `Num User Tables` is 0 since there are no user tables created yet. YugaByte DB version number is also shown for your reference. 

![master-home](/images/admin/master-home-docker.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement. 

### 3.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 3 tservers along with the time since they last connected to this master via their regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all the 3 tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-docker.png)

