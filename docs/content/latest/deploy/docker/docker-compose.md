---
title: Docker
linkTitle: Docker
description: Docker
aliases:
 - /admin/docker-compose/
 - /latest/admin/docker-compose/
menu:
  latest:
    parent: deploy
    name: Docker
    identifier: docker-compose
    weight: 625
type: page
isTocNested: false
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/docker/docker-swarm" class="nav-link">
      <i class="fas fa-layer-group"></i>
      Docker Swarm
    </a>
  </li>
  <li>
    <a href="/latest/deploy/docker/docker-compose" class="nav-link active">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker Compose
    </a>
  </li>
</ul>

Use [docker-compose](https://docs.docker.com/compose/overview/) utility to create and manage YugaByte DB local clusters. Note that this approach is not recommended for multi-node clusters used for performance testing and production environments.

## 1. Create a single node cluster

### Pull the container

Pull the container from docker hub registry

```sh
$ docker pull yugabytedb/yugabyte
```


### Create a docker-compose.yaml file

<div class='copy'></div>
```sh
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
                "--start_pgsql_proxy", 
                "--tserver_master_addrs=yb-master-n1:7100"]
      ports:
      - "9042:9042"
      - "6379:6379"
      - "5433:5433"
      - "9000:9000"
      environment:
        SERVICE_5433_NAME: ysql
        SERVICE_9042_NAME: ycql
        SERVICE_6379_NAME: yedis
        SERVICE_9000_NAME: yb-tserver
      depends_on:
      - yb-master
```

### Start the cluster


```sh
$ docker-compose up -d
```

## 2. Initialize the APIs

Enable the YSQL API by running the following command.

```sh
$ docker exec -it yb-master-n1 bash  -c "YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses=yb-master-n1:7100 /home/yugabyte/postgres/bin/initdb -D /tmp/yb_pg_initdb_tmp_data_dir -U postgres"
```

Optionally, you can enable YEDIS API by running the following command.

```sh
$ docker exec -it yb-master-n1 /home/yugabyte/bin/yb-admin --master_addresses yb-master-n1:7100 setup_redis_table
```

Clients can now connect to the YSQL(Beta) API at localhost:5433, YCQL API at localhost:9042 and YEDIS API at localhost:6379. The yb-master admin service is available at http://localhost:7000.

## 3. Test the APIs

Follow the instructions in the [Quick Start](../../quick-start/) section with Docker.

## 4. Stop the cluster

```sh
$ docker-compose down
```

