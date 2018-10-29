---
title: docker-compose
linkTitle: docker-compose
description: docker-compose
menu:
  v1.0:
    identifier: docker-compose
    parent: admin
    weight: 2430
---

Use the popular [docker-compose](https://docs.docker.com/compose/overview/) utility to create and manage YugaByte DB local clusters.

## 1. Create a single node cluster

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
      - "5433:5433"
      - "9000:9000"
      environment:
        SERVICE_9042_NAME: cassandra
        SERVICE_6379_NAME: redis
        SERVICE_5433_NAME: postgresql
        SERVICE_9000_NAME: yb-tserver
      depends_on:
      - yb-master
```

### Start the cluster

```{.sh .copy .separator-dollar}
$ docker-compose up -d
```

## 2. Setup the YEDIS API

```{.sh .copy .separator-dollar}
$ docker exec -it yb-master-n1 /home/yugabyte/bin/yb-admin --master_addresses yb-master-n1:7100 setup_redis_table
```

Clients can now connect to the YCQL service at localhost:9042, to the YEDIS API at localhost:6379, and to the PostgreSQL(Beta) service at localhost:5433. The yb-master admin service is available at http://localhost:7000.


## 3. Test the various YugaByte DB APIs

Follow the instructions in the Quick Start section with Docker using the links below.

- [YCQL API](../../quick-start/test-cassandra/#docker)

- [YEDIS API](../../quick-start/test-redis/#docker)

- [PostgreSQL API (Beta)](../../quick-start/test-postgresql/#docker)


## 4. Stop the cluster

```{.sh .copy .separator-dollar}
$ docker-compose down
```
