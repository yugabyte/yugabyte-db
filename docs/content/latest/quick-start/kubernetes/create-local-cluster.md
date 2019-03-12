## 1. Create a 1 node cluster with replication factor 1 

```sh
$ kubectl apply -f yugabyte-statefulset-rf-1.yaml
```
  
```
service/yb-masters created
service/yb-master-ui created
statefulset.apps/yb-master created
service/yb-tservers created
statefulset.apps/yb-tserver created
```

By default, the above command will create a 1 node cluster with Replication Factor (RF) 1. This cluster has 1 pod of yb-master and yb-tserver each. If you want to create a 3 node local cluster with RF 3, then simply change the replica count of yb-master and yb-tserver in the yaml file to 3.

## 2. Check cluster status

Run the command below to see that we now have two services with 1 pods each - 1 `yb-master` pod (yb-master-1) and 1 `yb-tserver` pods (yb-tserver-1) running. Roles played by these pods in a YugaByte DB cluster (aka Universe) is explained in detail [here](../../architecture/concepts/universe/).

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/1       ContainerCreating   0          5s
yb-tserver-0   0/1       ContainerCreating   0          4s
```

Eventually all the pods will have the `Running` state.

Run the following command to initialize the YSQL API. Note that this step can take a few minutes depending on the resource utilization of your Kubernetes environment.

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-tserver-0   1/1       Running   0          12s
```

## 3. Initialize the YSQL API

```sh
$ kubectl exec -it yb-master-0 bash --  -c "YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses=yb-master-0.yb-masters.default.svc.cluster.local:7100 /home/yugabyte/postgres/bin/initdb -D /tmp/yb_pg_initdb_tmp_data_dir -U postgres"
```

Clients can now connect to this YugaByte DB universe using YSQL and YCQL APIs on the 5433 and 9042 ports respectively.

## 4. Check cluster status via Kubernetes

You can see the status of the 3 services by simply running the following command.

```sh
$ kubectl get services
```

```
NAME           TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                                        AGE
kubernetes     ClusterIP      10.96.0.1       <none>        443/TCP                                        13m
yb-master-ui   LoadBalancer   10.110.45.247   <pending>     7000:32291/TCP                                 11m
yb-masters     ClusterIP      None            <none>        7000/TCP,7100/TCP                              11m
yb-tservers    ClusterIP      None            <none>        9000/TCP,9100/TCP,9042/TCP,6379/TCP,5433/TCP   11m
```

## 5. Check cluster status with Admin UI

In order to do this, we would need to access the UI on port 7000 exposed by any of the pods in the `yb-master` service. In order to do so, we find the URL for the yb-master-ui LoadBalancer service.

```sh
$ minikube service  yb-master-ui --url
```

```
http://192.168.99.100:31283
```

Now, you can view the [yb-master-0 Admin UI](../../admin/yb-master/#admin-ui) is available at the above URL.

### 5.1 Overview and Master status

The yb-master-0 home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is 0 since there are no user tables created yet. YugaByte DB version is also shown for your reference. 

![master-home](/images/admin/master-home-kubernetes-rf1.png)

The Masters section highlights the 1 yb-master along its corresponding cloud, region and zone placement information. 

### 5.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 1 tserver along with the time since it last connected to this master via regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all available tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tservers.

![tserver-list](/images/admin/master-tservers-list-kubernetes-rf1.png)
