## 1. Create a 3 node cluster with replication factor 3 

Run the following command to create the cluster.

```sh
$ kubectl apply -f yugabyte-statefulset.yaml
service "yb-masters" created
statefulset "master" created
service "yb-tservers" created
statefulset "tserver" created
```

## 2. Check cluster status

Run the command below to see that we now have two services with 3 pods each - 3 `yb-master` pods (master-1,master-2,master-3) and 3 `yb-tserver` pods (tserver-1,tserver-2,tserver-3) running. Roles played by these pods in a YugaByte cluster (aka Universe) is explained in detail [here](/architecture/concepts/universe/).

```sh
$ kubectl get pods
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/1       ContainerCreating   0          5s
yb-master-1    0/1       ContainerCreating   0          5s
yb-master-2    1/1       Running             0          5s
yb-tserver-0   0/1       ContainerCreating   0          4s
yb-tserver-1   0/1       ContainerCreating   0          4s
yb-tserver-2   0/1       ContainerCreating   0          4s
```

Eventually all the pods will have the `Running` state.

```sh
$ kubectl get pods
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-master-1    1/1       Running   0          13s
yb-master-2    1/1       Running   0          13s
yb-tserver-0   1/1       Running   1          12s
yb-tserver-1   1/1       Running   1          12s
yb-tserver-2   1/1       Running   1          12s
```


## 3. Initialize the Redis API

Initialize Redis by running the following `yb-admin` command. This will initialize the Redis API and DB in the YugaByteDB kubernetes universe we just setup.

```
kubectl exec -it yb-master-0 /home/yugabyte/bin/yb-admin -- --master-addresses yb-master-0.yb-masters.default.svc.cluster.local:7100,yb-master-1.yb-masters.default.svc.cluster.local:7100,yb-master-2.yb-masters.default.svc.cluster.local:7100 setup_redis_table
...
I0127 19:38:10.358551   115 client.cc:1292] Created table system_redis.redis of type REDIS_TABLE_TYPE
I0127 19:38:10.358872   115 yb-admin_client.cc:400] Table 'system_redis.redis' created.
```

Clients can now connect to this YugaByte universe using Cassandra and Redis APIs.



## 4. Check cluster status with Admin UI

In order to do this, we would need to access the UI on port 7000 exposed by any of the pods in the `yb-master` service (one of `master-0`, `master-1` or `master-2`). Let us set up a network route to access `master-0` on port 7000 from our localhost. You can do this by running the following command.

```
kubectl port-forward yb-master-0 7000
```

Now, you can view the [master-0 Admin UI](/admin/yb-master/#admin-ui) is available at http://localhost:7000.

### 4.1 Overview and Master status

The yb-master-n1 home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 3 and `Num Nodes (TServers)` as 3. The `Num User Tables` is set to 1 because of the `system_redis.redis` table that `yb-docker-ctl` auto-creates in order to turn on YugaByte Redis service. YugaByte DB version number is also shown for your reference. 

![master-home](/images/admin/master-home-kubernetes.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement. 

### 4.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 3 tservers along with the time since they last connected to this master via their regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all the 3 tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-kubernetes.png)

