---
date: 2016-03-09T00:11:02+01:00
title: yb-docker-ctl
weight: 242
---

`yb-docker-ctl` is a simple command line interface for administering local Docker clusters. It manages the [`yb-master`] (/admin/yb-master/) and [`yb-tserver`] (/admin/yb-tserver/) containers to perform the necessary administration.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ .$ ./yb-docker-ctl -h
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

```sh
$ ./yb-docker-ctl create
latest: Pulling from yugabytedb/yugabyte
d9aaf4d82f24: Pull complete 
5623eec13c9f: Pull complete 
a5a887f7d7b5: Pull complete 
be82c028c6d1: Pull complete 
Digest: sha256:638c6bb34143ed3a0dc2e5407dfd43987f235c7f7d0c9985136d9dd8021c30dc
Status: Downloaded newer image for yugabytedb/yugabyte:latest
docker run --name yb-master-n1 -p 7000:7000 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n1:7100
Adding node yb-master-n1
docker run --name yb-master-n2 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n2:7100
Adding node yb-master-n2
docker run --name yb-master-n3 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-master --fs_data_dirs=/mnt/disk0,/mnt/disk1 --master_addresses=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-master-n3:7100
Adding node yb-master-n3
docker run --name yb-tserver-n1 -p 9000:9000 -p 9042:9042 -p 6379:6379 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n1:9100
Adding node yb-tserver-n1
docker run --name yb-tserver-n2 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n2:9100
Adding node yb-tserver-n2
docker run --name yb-tserver-n3 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=yb-master-n1:7100,yb-master-n2:7100,yb-master-n3:7100 --rpc_bind_addresses=yb-tserver-n3:9100
Adding node yb-tserver-n3
PID        Type       Node       URL                       Status          Started At          
26132      tserver    n3         http://172.18.0.7:9000    Running         2017-10-20T17:54:54.99459154Z
25965      tserver    n2         http://172.18.0.6:9000    Running         2017-10-20T17:54:54.412377451Z
25846      tserver    n1         http://172.18.0.5:9000    Running         2017-10-20T17:54:53.806993683Z
25660      master     n3         http://172.18.0.4:7000    Running         2017-10-20T17:54:53.197652566Z
25549      master     n2         http://172.18.0.3:7000    Running         2017-10-20T17:54:52.640188158Z
25438      master     n1         http://172.18.0.2:7000    Running         2017-10-20T17:54:52.084772289Z
```

- Create a 5 node local cluster with replication factor 5. 

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. With the [add_node](/admin/yb-docker-ctl/#add-a-node) and [remove_node](/admin/yb-docker-ctl/#remove-a-node) commands the size of the cluster can thereafter be expanded or shrinked as necessary. 

```sh
$ ./bin/yb-docker-ctl create --rf 5
```

## Check cluster status

Get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer.

```sh
$ ./bin/yb-docker-ctl status
PID        Type       Node       URL                       Status          Started At          
2004       tserver    n3         http://172.18.0.7:9000    Running         2017-10-20T23:04:21.045013155Z
1794       tserver    n2         http://172.18.0.6:9000    Running         2017-10-20T23:04:20.444355295Z
1695       tserver    n1         http://172.18.0.5:9000    Running         2017-10-20T23:04:19.961339125Z
1463       master     n3         http://172.18.0.4:7000    Running         2017-10-20T23:04:19.37467522Z
1347       master     n2         http://172.18.0.3:7000    Running         2017-10-20T23:04:18.871594502Z
1235       master     n1         http://172.18.0.2:7000    Running         2017-10-20T23:04:18.374773749Z
```

## Add a node


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yb-docker-ctl add_node
docker run --name yb-tserver-n4 --net yb-net --detach yugabytedb/yugabyte:latest /home/yugabyte/bin/yb-tserver --fs_data_dirs=/mnt/disk0,/mnt/disk1 --tserver_master_addrs=04:7100,04:7100,04:7100 --rpc_bind_addresses=yb-tserver-n4:9100
Adding node yb-tserver-n4
```

## Remove a node

Remove a node from the cluster by executing the following command. The command takes the node_id of the node to be removed as input.

### Help

```sh
$ ./bin/yb-docker-ctl remove_node -h
usage: yb-docker-ctl remove_node [-h] node_id

positional arguments:
  node_id     The index of the tserver to remove

optional arguments:
  -h, --help  show this help message and exit
```

### Example

```sh
$ ./bin/yb-docker-ctl remove_node 4
Stopping node :yb-tserver-n4
```

## Destroy cluster

The command below destroys the cluster which includes deleting the data directories.

```sh
$ ./bin/yb-docker-ctl destroy
```

## Upgrade container image

The command below upgrades the YugaByte DB image to the latest version.

```sh
$ docker pull yugabytedb/yugabyte
```