## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```{.sh .copy .separator-dollar}
$ kubectl apply -f yugabyte-statefulset.yaml
```

Check the Kubernetes dashboard to see the 3 yb-tserver and 3 yb-master pods representing the 3 nodes of the cluster.

```{.sh .copy .separator-dollar}
$ minikube dashboard
```

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

Connect to cqlsh on node 1.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a Cassandra keyspace and a table.

```{.sql .copy .separator-gt}
cqlsh> CREATE KEYSPACE users;
```
```{.sql .copy .separator-gt}
cqlsh> CREATE TABLE users.profile (id bigint PRIMARY KEY,
	                               email text,
	                               password text,
	                               profile frozen<map<text, text>>);
```

## 2. Check cluster status with Admin UI

In order to do this, we would need to access the UI on port 7000 exposed by any of the pods in the `yb-master` service (one of `yb-master-0`, `yb-master-1` or `yb-master-2`). In order to do so, we find the URL for the yb-master-ui LoadBalancer service.

```{.sh .copy .separator-dollar}
$ minikube service  yb-master-ui --url
```
```sh
http://192.168.99.v1.0:31283
```

Now, you can view the [yb-master-0 Admin UI](../../admin/yb-master/#admin-ui) is available at the above URL.


## 3. Observe data sizes per node

You can check a lot of the per-node stats by browsing to the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total data size per node (labeled as Total SST File Sizes) as well as the total memory used per node (labeled as RAM Used) are highlighted in the screenshot below. Note that both of these metrics are roughly the same across all the nodes indicating uniform usage across the nodes.

![Tablet count, data and memory sizes with 3 nodes](/images/ce/auto-rebalancing-3-nodes-docker.png)

## 4. Add a node and observe data rebalancing

Add a node to the universe.

```{.sh .copy .separator-dollar}
$ kubectl scale statefulset yb-tserver --replicas=4
```

Now we should have 4 nodes. Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. As you refresh, you should see the new node getting more and more tablets, which would cause it to get more data as well as increase its memory footprint. Finally, all the 4 nodes should end up with a similar data distribution and memory usage.

![Tablet count, data and memory sizes with 4 nodes](/images/ce/auto-rebalancing-4-nodes-docker.png)

YugaByte DB automatically balances the tablet leaders and followers of a universe by moving them in a rate-limited manner into the newly added nodes. This automatic balancing of the data is completely transparent to the application logic.

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```
