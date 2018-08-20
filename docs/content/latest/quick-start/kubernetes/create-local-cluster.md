## 1. Create a 3 node cluster with replication factor 3 

Run the following command to create the cluster.

```{.sh .copy .separator-dollar}
$ kubectl apply -f yugabyte-statefulset.yaml
```
```sh
service "yb-masters" created
statefulset "yb-master" created
service "yb-tservers" created
statefulset "yb-tserver" created
```

## 2. Check cluster status

Run the command below to see that we now have two services with 3 pods each - 3 `yb-master` pods (yb-master-1,yb-master-2,yb-master-3) and 3 `yb-tserver` pods (yb-tserver-1,yb-tserver-2,yb-tserver-3) running. Roles played by these pods in a YugaByte DB cluster (aka Universe) is explained in detail [here](../../architecture/concepts/universe/).

```{.sh .copy .separator-dollar}
$ kubectl get pods
```
```sh
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/1       ContainerCreating   0          5s
yb-master-1    0/1       ContainerCreating   0          5s
yb-master-2    1/1       Running             0          5s
yb-tserver-0   0/1       ContainerCreating   0          4s
yb-tserver-1   0/1       ContainerCreating   0          4s
yb-tserver-2   0/1       ContainerCreating   0          4s
```

Eventually all the pods will have the `Running` state.

```{.sh .copy .separator-dollar}
$ kubectl get pods
```
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-master-1    1/1       Running   0          13s
yb-master-2    1/1       Running   0          13s
yb-tserver-0   1/1       Running   1          12s
yb-tserver-1   1/1       Running   1          12s
yb-tserver-2   1/1       Running   1          12s
```


## 3. Initialize the Redis-compatible YEDIS API

Initialize Redis-compatible YEDIS API in the YugaByte DB Universe we just setup by running the following `yb-admin` command. 

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-master-0 /home/yugabyte/bin/yb-admin -- --master_addresses yb-master-0.yb-masters.default.svc.cluster.local:7100,yb-master-1.yb-masters.default.svc.cluster.local:7100,yb-master-2.yb-masters.default.svc.cluster.local:7100 setup_redis_table
```
```sh
...
I0127 19:38:10.358551   115 client.cc:1292] Created table system_redis.redis of type REDIS_TABLE_TYPE
I0127 19:38:10.358872   115 yb-admin_client.cc:400] Table 'system_redis.redis' created.
```

Clients can now connect to this YugaByte DB universe using Cassandra and Redis APIs on the 9042 and 6379 ports respectively.

## 4. Check cluster status via Kubernetes

You can see the status of the 3 services by simply running the following command.

```{.sh .copy .separator-dollar}
$ kubectl get services
```
```sh
NAME           TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                               AGE
kubernetes     ClusterIP      10.96.0.1       <none>        443/TCP                               10m
yb-master-ui   LoadBalancer   10.102.121.64   <pending>     7000:31283/TCP                        8m
yb-masters     ClusterIP      None            <none>        7000/TCP,7100/TCP                     8m
yb-tservers    ClusterIP      None            <none>        9000/TCP,9100/TCP,9042/TCP,6379/TCP   8m
```

## 5. Check cluster status with Admin UI

In order to do this, we would need to access the UI on port 7000 exposed by any of the pods in the `yb-master` service (one of `yb-master-0`, `yb-master-1` or `yb-master-2`). In order to do so, we find the URL for the yb-master-ui LoadBalancer service.

```{.sh .copy .separator-dollar}
$ minikube service  yb-master-ui --url
```
```sh
http://192.168.99.100:31283
```

Now, you can view the [yb-master-0 Admin UI](../../admin/yb-master/#admin-ui) is available at the above URL.

### 5.1 Overview and Master status

The yb-master-0 home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 3 and `Num Nodes (TServers)` as 3. The `Num User Tables` is 0 since there are no user tables created yet. YugaByte DB version is also shown for your reference. 

![master-home](/images/admin/master-home-kubernetes.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement. 

### 5.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 3 tservers along with the time since they last connected to this master via their regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all the 3 tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-kubernetes.png)

