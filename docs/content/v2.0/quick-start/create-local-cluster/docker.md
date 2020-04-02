---
title: 2. Create a local cluster
linkTitle: 2. Create a local cluster
description: Create a local cluster
block_indexing: true
menu:
  v2.0:
    parent: quick-start
    name: 2. Create a local cluster
    identifier: create-local-cluster-3-docker
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
    <a href="/latest/quick-start/create-local-cluster/docker" class="nav-link active">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/create-local-cluster/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

## 1. Create a local cluster

You can use the [`yb-docker-ctl`](../../../admin/yb-docker-ctl/) utility, downloaded in the previous step, to create and administer a containerized local cluster.

To quickly create a 1-node or 3-node local cluster using Docker, follow the steps below. For details on using the `yb-docker-ctl create` command and the cluster configuration, see [Create a local cluster](../../../admin/yb-docker-ctl/#create-cluster) in the utility reference.

### Create a 1-node cluster with RF=1

To create a 1-node cluster with a replication factor (RF) of 1, run the default `yb-ctl create` command.

```sh
$ ./yb-docker-ctl create
```

### Create a 3-node cluster with RF=3

To run a distributed SQL cluster locally, run the following `yb-docker-ctl` command to create a 3-node YugabyteDB cluster with a replication factor (RF) of 3.

```sh
$ ./yb-docker-ctl create --rf 3
```

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

## 2. Check cluster status with yb-docker-ctl

Run the command below to see that we now have 1 `yb-master` (yb-master-n1) and 1 `yb-tserver` (yb-tserver-n1) containers running on this localhost. Roles played by these containers in a YugabyteDB cluster are explained in detail [here](../../../architecture/concepts/universe/).

```sh
$ ./yb-docker-ctl status
```

```
ID             PID        Type       Node                 URL                       Status          Started At
921494a8058d   5547       tserver    yb-tserver-n1        http://192.168.64.5:9000  Running         2018-10-18T22:02:50.187976253Z
feea0823209a   5039       master     yb-master-n1         http://192.168.64.2:7000  Running         2018-10-18T22:02:47.163244578Z
```

## 3. Check cluster status with Admin UI

The [yb-master-n1 Admin UI](../../../reference/configuration/yb-master/#admin-ui) is available at `http://localhost:7000` and the [yb-tserver-n1 Admin UI](../../../reference/configuration/yb-tserver/#admin-ui) is available at `http://localhost:9000`. To avoid port conflicts, other YB-Master and YB-TServer services do not have their admin ports mapped to `localhost`.

{{< note title="Note" >}}

Clients connecting to the cluster will connect to only yb-tserver-n1 even if you used yb-docker-ctl to create a multi-node local cluster. In case of Docker for Mac, routing [traffic directly to containers](https://docs.docker.com/docker-for-mac/networking/#known-limitations-use-cases-and-workarounds) is not even possible today. Since only 1 node will receive the incoming client traffic, throughput expected for Docker-based local clusters can be significantly lower than binary-based local clusters.

{{< /note >}}

### Overview and YB-Master status

The yb-master-n1 home page shows that we have a cluster (or universe) with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is `0` since there are no user tables created yet. YugabyteDB version number is also shown for your reference.

![master-home](/images/admin/master-home-docker-rf1.png)

The Masters section highlights the 3 masters along with their corresponding cloud, region and zone placement.

### YB-TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 1 tservers along with the time since it last connected to this master via regular heartbeats. Additionally, we can see that the `Load (Num Tablets)` is balanced across all available tservers. These tablets are the shards of the user tables currently managed by the cluster (which in this case is the `system_redis.redis` table). As new tables get added, new tablets will get automatically created and distributed evenly across all the available tservers.

![master-home](/images/admin/master-tservers-list-docker-rf1.png)
