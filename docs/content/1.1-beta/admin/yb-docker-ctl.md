---
title: yb-docker-ctl
linkTitle: yb-docker-ctl
description: yb-docker-ctl
menu:
  1.1-beta:
    identifier: yb-docker-ctl
    parent: admin
    weight: 2420
aliases:
  - admin/yb-docker-ctl
---

`yb-docker-ctl` is a simple command line interface for administering local Docker clusters. It manages the [`yb-master`](../yb-master/) and [`yb-tserver`](../yb-tserver/) containers to perform the necessary administration.

## Download

```{.sh .copy .separator-dollar}
$ mkdir ~/yugabyte && cd ~/yugabyte
```
```{.sh .copy .separator-dollar}
$ wget https://downloads.yugabyte.com/yb-docker-ctl && chmod +x yb-docker-ctl
```


## Help command

Use the **-\-help** option to see all the commands supported.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl -h
```
```sh
usage: yb-docker-ctl [-h] {create,status,destroy,add_node,remove_node} ...

YugaByte Docker Container Control

positional arguments:
  {create,status,destroy,add_node,remove_node}
                        Commands
    create              Create YugaByte Cluster
    status              Check YugaByte Cluster status
    destroy             Destroy YugaByte Cluster
    add_node            Add a new YugaByte Cluster Node
    remove_node         Stop a YugaByte Cluster Node

optional arguments:
  -h, --help            show this help message and exit
```

## Create cluster

- Create a 3 node local cluster with replication factor 3. 

Each of these initial nodes run a `yb-tserver` process and a `yb-master` process. Note that the number of yb-masters in a cluster has to equal to the replication factor for the cluster to be considered as operating normally and the number of yb-tservers is equal to be the number of nodes.

Note that the create command pulls the latest `yugabytedb/yugabyte` image at the outset in case the image is not yet downloaded or is not the latest.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create
```
```sh
docker run --name yb-master-n1 --privileged -p 7000:7000 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n1:7100
Adding node yb-master-n1
docker run --name yb-master-n2 --privileged --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n2:7100
Adding node yb-master-n2
docker run --name yb-master-n3 --privileged --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n3:7100
Adding node yb-master-n3
docker run --name yb-tserver-n1 --privileged -p 9000:9000 -p 9042:9042 -p 6379:6379 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n1:9100
Adding node yb-tserver-n1
docker run --name yb-tserver-n2 --privileged --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n2:9100
Adding node yb-tserver-n2
docker run --name yb-tserver-n3 --privileged --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n3:9100
Adding node yb-tserver-n3
PID        Type       Node                 URL                       Status          Started At          
11818      tserver    yb-tserver-n3        http://172.19.0.7:9000    Running         2017-11-28T23:33:00.369124907Z
11632      tserver    yb-tserver-n2        http://172.19.0.6:9000    Running         2017-11-28T23:32:59.874963849Z
11535      tserver    yb-tserver-n1        http://172.19.0.5:9000    Running         2017-11-28T23:32:59.444064946Z
11350      master     yb-master-n3         http://172.19.0.4:9000    Running         2017-11-28T23:32:58.899308826Z
11231      master     yb-master-n2         http://172.19.0.3:9000    Running         2017-11-28T23:32:58.403788411Z
11133      master     yb-master-n1         http://172.19.0.2:9000    Running         2017-11-28T23:32:57.905097927Z
```

- Create a 5 node local cluster with replication factor 5. 

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. With the [add_node](#add-a-node) and [remove_node](#remove-a-node) commands the size of the cluster can thereafter be expanded or shrinked as necessary. 

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create --rf 5
```

## Check cluster status

Get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl status
```
```sh
PID        Type       Node                 URL                       Status          Started At          
11818      tserver    yb-tserver-n3        http://172.19.0.7:9000    Running         2017-11-28T23:33:00.369124907Z
11632      tserver    yb-tserver-n2        http://172.19.0.6:9000    Running         2017-11-28T23:32:59.874963849Z
11535      tserver    yb-tserver-n1        http://172.19.0.5:9000    Running         2017-11-28T23:32:59.444064946Z
11350      master     yb-master-n3         http://172.19.0.4:9000    Running         2017-11-28T23:32:58.899308826Z
11231      master     yb-master-n2         http://172.19.0.3:9000    Running         2017-11-28T23:32:58.403788411Z
11133      master     yb-master-n1         http://172.19.0.2:9000    Running         2017-11-28T23:32:57.905097927Z
```

## Add a node


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl add_node
```
```sh
docker run --name yb-tserver-n4 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=04:7100,04:7100,04:7100 --rpc_bind_addresses=yb-tserver-n4:9100
Adding node yb-tserver-n4
```

## Remove a node

Remove a node from the cluster by executing the following command. The command takes the node_id of the node to be removed as input.

### Help

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl remove_node --help
```
```sh
usage: yb-docker-ctl remove_node [-h] node

positional arguments:
  node_id        Index of the node to remove

optional arguments:
  -h, --help  show this help message and exit
```

### Example

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl remove_node 3
```
```sh
Stopping node :yb-tserver-n3
```

## Destroy cluster

The command below destroys the cluster which includes deleting the data directories.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```

## Upgrade container image

The command below upgrades the YugaByte DB image to the latest version.

```{.sh .copy .separator-dollar}
$ docker pull yugabytedb/yugabyte
```
