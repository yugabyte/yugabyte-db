---
title: Create a local YugabyteDB cluster on Kubernetes (Minikube)
headerTitle: 2. Create a local cluster
linkTitle: 2. Create a local cluster
description: Create a local YugabyteDB cluster on Kubernetes (Minikube) in less than five minutes.
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
    <a href="../macos/" class="nav-link">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="../linux/" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>

  <li >
    <a href="../docker/" class="nav-link">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>

  <li >
    <a href="../kubernetes/" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

## Create a local cluster

Create a YugabyteDB cluster in Minikube using the commands below. Note that for Helm, you have to first create a namespace.

```sh
$ kubectl create namespace yb-demo
$ helm install yb-demo yugabytedb/yugabyte \
--set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
replicas.master=1,replicas.tserver=1 --namespace yb-demo
```

Note that in Minikube, the LoadBalancers for `yb-master-ui` and `yb-tserver-service` will remain in pending state since load balancers are not available in a Minikube environment. If you would like to turn off these services then pass the `enableLoadBalancer=False` flag as shown below.

```sh
$ helm install yb-demo yugabytedb/yugabyte \
--set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
replicas.master=1,replicas.tserver=1,enableLoadBalancer=False --namespace yb-demo
```

## Check cluster status with kubectl

Run the following command to see that you now have two services with one pod each — 1 yb-master pod (`yb-master-0`) and 1 yb-tserver pod (`yb-tserver-0`) running. For details on the roles of these pods in a YugabyteDB cluster (aka Universe), see [Universe](../../../architecture/concepts/universe/) in the Concepts section.

```sh
$ kubectl --namespace yb-demo get pods
```

```output
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/2       ContainerCreating   0          5s
yb-tserver-0   0/2       ContainerCreating   0          4s
```

Eventually, all the pods will have the `Running` state.

```output
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    2/2       Running   0          13s
yb-tserver-0   2/2       Running   0          12s
```

To see the status of the three services, run the following command.

```sh
$ kubectl --namespace yb-demo get services
```

```output
NAME                 TYPE           CLUSTER-IP     EXTERNAL-IP   PORT(S)                                        AGE
yb-master-ui         LoadBalancer   10.98.66.255   <pending>     7000:31825/TCP                                 119s
yb-masters           ClusterIP      None           <none>        7100/TCP,7000/TCP                              119s
yb-tserver-service   LoadBalancer   10.106.5.69    <pending>     6379:31320/TCP,9042:30391/TCP,5433:30537/TCP   119s
yb-tservers          ClusterIP      None           <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP   119s
```

## Check cluster status with Admin UI

To check the cluster status, you need to access the Admin UI on port `7000` exposed by the `yb-master-ui` service. To do so, you need to port forward the port.

```sh
$ kubectl --namespace yb-demo port-forward svc/yb-master-ui 7000:7000
```

Now, you can view the [yb-master-0 Admin UI](../../../reference/configuration/yb-master/#admin-ui) at <http://localhost:7000>.

### Overview and YB-Master status

The YB-Master home page shows that you have a cluster (or universe) with a replication factor of 1, a single node, and no tables. The YugabyteDB version is also displayed.

![master-home](/images/admin/master-home-kubernetes-rf1.png)

The **Masters** section highlights the 1 YB-Master along with its corresponding cloud, region, and zone placement.

### YB-TServer status

Click **See all nodes** to go to the **Tablet Servers** page, which lists the YB-TServer along with the time since it last connected to the YB-Master using regular heartbeats.

![tserver-list](/images/admin/master-tservers-list-kubernetes-rf1.png)

## Next step

[Explore YSQL](../../explore/ysql/)
