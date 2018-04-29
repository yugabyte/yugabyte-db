---
title: docker-compose
linkTitle: docker-compose
description: docker-compose
menu:
  latest:
    identifier: docker-compose
    parent: admin
    weight: 2430
aliases:
  - admin/docker-compose
---

Use the popular [docker-compose](https://docs.docker.com/compose/overview/) utility to create and manage YugaByte DB local clusters.

## Create a single node cluster

### Pull the container

```{.sh .copy .separator-dollar}
# pull the container from docker hub registry
$ docker pull yugabytedb/yugabyte
```

### Create a docker-compose.yaml file

```{.sh .copy}
version: '2'

services:
  yb-master:
      image: yugabytedb/yugabyte:latest
      container_name: yb-master-n1
      command: [ "/home/yugabyte/bin/yb-master", 
                "--fs_data_dirs=/mnt/disk0,/mnt/disk1", 
                "--master_addresses=yb-master-n1:7100", 
                "--replication_factor=1"]
      ports:
      - "7000:7000"
      environment:
        SERVICE_7000_NAME: yb-master

  yb-tserver:
      image: yugabytedb/yugabyte:latest
      container_name: yb-tserver-n1
      command: [ "/home/yugabyte/bin/yb-tserver", 
                "--fs_data_dirs=/mnt/disk0,/mnt/disk1", 
                "--tserver_master_addrs=yb-master-n1:7100"]
      ports:
      - "9042:9042"
      - "6379:6379"
      - "9000:9000"
      environment:
        SERVICE_9042_NAME: cassandra
        SERVICE_6379_NAME: redis
        SERVICE_9000_NAME: yb-tserver
      depends_on:
      - yb-master
```

### Start the cluster

```{.sh .copy .separator-dollar}
$ docker-compose up -d
```

Clients can now connect to YugaByte’s CQL service at http://localhost:9042 and to YugaByte’s Redis service at http://localhost:6379. The yb-master admin service is available at http://localhost:7000.

## Connect to YCQL and YEDIS APIs

```{.sh .copy .separator-dollar}
# connect to cassandra service on port 9042 via cqlsh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/cqlsh
```


```{.sh .copy .separator-dollar}
# connect to redis service on port 6379 via redis-cli
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/redis-cli
```

## Stop the cluster

```{.sh .copy .separator-dollar}
$ docker-compose down
```
