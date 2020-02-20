---
title: 2. Create a local cluster
linkTitle: 2. Create a local cluster
description: Create a local cluster
menu:
  latest:
    parent: quick-start
    name: 2. Create a local cluster
    identifier: create-local-cluster-4-kubernetes
    weight: 120
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/quick-start/create-local-cluster/macos" class="nav-link">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/create-local-cluster/linux" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/create-local-cluster/docker" class="nav-link">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/create-local-cluster/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>


## 1. Create a local cluster

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

This command creates a one-node cluster with a replication Factor (RF) of 1. This cluster has one pod of `yb-master` and `yb-tserver` each. To create a three-node local cluster with an RF of 3, then simply change the replica count of `yb-master` and `yb-tserver` in the YAML file to `3`.

## 2. Check cluster status with kubectl

Run the following command to see that you now have two services with one pod each — 1 `yb-master` pod (`yb-master-0`) and 1 `yb-tserver` pod (`yb-tserver-0`) running. For details on the roles of these pods in a YugabyteDB cluster (aka Universe), see [Universe](../../architecture/concepts/universe/) in the Concepts section.

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/1       ContainerCreating   0          5s
yb-tserver-0   0/1       ContainerCreating   0          4s
```

Eventually, all the pods will have the `Running` state.

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-tserver-0   1/1       Running   0          12s
```

To see the status of the three services, run the following command.

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

## 3. Check cluster status with Admin UI

To check the cluster status, you need to access the Admin UI on port `7000` exposed by any of the pods in the `yb-master` service. In order to do so, you need to find the URL for the `yb-master-ui` LoadBalancer service.

```sh
$ minikube service  yb-master-ui --url
```

```
http://192.168.99.100:31283
```

Now, you can view the [yb-master-0 Admin UI](../../reference/configuration/yb-master/#admin-ui) at the URL above.

### Overview and YB-Master status

The `yb-master-0` home page shows that we have a cluster (or universe) with **Replication Factor** of 1 and **Num Nodes (TServers)** as `1`. The **Num User Tables** is `0` because there are no user tables created yet. The YugabyteDB version is also displayed for your reference.

![master-home](/images/admin/master-home-kubernetes-rf1.png)

The **Masters** section highlights the YB-Master service along its corresponding cloud, region and zone placement information.

### YB-TServer status

Click **See all nodes** to go to the **Tablet Servers** page where we can observe the one YB-TServer along with the time since it last connected to the YB-Master using regular heartbeats. Additionally, you can see that the **Load (Num Tablets)** is balanced across all available YB-TServers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available YB-TServers.

![tserver-list](/images/admin/master-tservers-list-kubernetes-rf1.png)
