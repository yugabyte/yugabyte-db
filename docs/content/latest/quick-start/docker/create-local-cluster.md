## 1. Create a local cluster 
  
We will use the [`yb-docker-ctl`](../../admin/yb-docker-ctl/) utility downloaded in the previous step to create and administer a containerized local cluster. Detailed output for the *create* command is available in [yb-docker-ctl Reference](../../admin/yb-docker-ctl/#create-cluster).
  

```sh
$ ./yb-docker-ctl create
```

By default, the above command will create a 1 node cluster with Replication Factor (RF) 1. You can create run a 3 node cluster with RF 3 by using the command below.

```sh
$ ./yb-docker-ctl create --rf 3
```

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

## 2. Check cluster status with yb-docker-ctl

Run the command below to see that we now have 1 `yb-master` (yb-master-n1) and 1 `yb-tserver` (yb-tserver-n1) containers running on this localhost. Roles played by these containers in a YugaByte DB cluster are explained in detail [here](../../architecture/concepts/universe/).

```sh
$ ./yb-docker-ctl status
```

```
ID             PID        Type       Node                 URL                       Status          Started At
921494a8058d   5547       tserver    yb-tserver-n1        http://192.168.64.5:9000  Running         2018-10-18T22:02:50.187976253Z
feea0823209a   5039       master     yb-master-n1         http://192.168.64.2:7000  Running         2018-10-18T22:02:47.163244578Z
```

## 3. Check cluster status with Admin UI

The [yb-master-n1 Admin UI](../../admin/yb-master/#admin-ui) is available at http://localhost:7000 and the [yb-tserver-n1 Admin UI](../../admin/yb-tserver/#admin-ui) is available at http://localhost:9000. Other masters and tservers do not have their admin ports mapped to localhost to avoid port conflicts. 

**NOTE:**
Clients connecting to the cluster will connect to only yb-tserver-n1 even if you used yb-docker-ctl to create a multi-node local cluster. In case of Docker for Mac, routing [traffic directly to containers](https://docs.docker.com/docker-for-mac/networking/#known-limitations-use-cases-and-workarounds) is not even possible today. Since only 1 node will receive the incoming client traffic, throughput expected for Docker-based local clusters can be significantly lower than binary-based local clusters.


### 3.1 Overview and Master status

The yb-master-n1 home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is 0 since there are no user tables created yet. YugaByte DB version number is also shown for your reference. 

![master-home](/images/admin/master-home-docker-rf1.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement. 

### 3.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 1 tservers along with the time since it last connected to this master via regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all available tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tservers.

![master-home](/images/admin/master-tservers-list-docker-rf1.png)
