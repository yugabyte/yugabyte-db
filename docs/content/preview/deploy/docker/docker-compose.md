---
title: Deploy local clusters using Docker Compose
headerTitle: Docker
linkTitle: Docker
description: Use Docker Compose to create and manage local YugabyteDB clusters.
aliases:
  - /admin/docker-compose/
  - /preview/admin/docker-compose/
menu:
  preview:
    parent: deploy
    name: Docker
    identifier: docker-1-compose
    weight: 625
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="{{< relref "./docker-compose.md" >}}" class="nav-link active">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker Compose
    </a>
  </li>
</ul>

Use [docker-compose](https://docs.docker.com/compose/overview/) utility to create and manage YugabyteDB local clusters. Note that this approach is not recommended for multi-node clusters used for performance testing and production environments. Refer to the [deployment checklist](../../../deploy/checklist/) to understand the configuration to create clusters.

## Prerequisites

[Docker](https://docs.docker.com/get-docker/) must be installed on your machine.

## Create a single-node cluster

You can create a single-node cluster as follows:

- Pull the container from Docker Hub registry using the following command:

  ```sh
  docker pull yugabytedb/yugabyte
  ```

- Create a `docker-compose.yaml` file, as follows:

  ```sh
  version: '2'
  
  volumes:
    yb-master-data-1:
    yb-tserver-data-1:
  
  services:
    yb-master:
        image: yugabytedb/yugabyte:latest
        container_name: yb-master-n1
        volumes:
        - yb-master-data-1:/mnt/master
        command: [ "/home/yugabyte/bin/yb-master",
                  "--fs_data_dirs=/mnt/master",
                  "--master_addresses=yb-master-n1:7100",
                  "--rpc_bind_addresses=yb-master-n1:7100",
                  "--replication_factor=1"]
        ports:
        - "7000:7000"
        environment:
          SERVICE_7000_NAME: yb-master
  
    yb-tserver:
        image: yugabytedb/yugabyte:latest
        container_name: yb-tserver-n1
        volumes:
        - yb-tserver-data-1:/mnt/tserver
        command: [ "/home/yugabyte/bin/yb-tserver",
                  "--fs_data_dirs=/mnt/tserver",
                  "--start_pgsql_proxy",
                  "--rpc_bind_addresses=yb-tserver-n1:9100",
                  "--tserver_master_addrs=yb-master-n1:7100"]
        ports:
        - "9042:9042"
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

- Start the cluster using the following command:

  ```sh
  docker-compose -f ./docker-compose.yaml up -d
  ```

## Initialize APIs

YCQL and YSQL APIs are enabled by default on the cluster.

## Test APIs

Testing APIs involves verifying that clients can connect to the YSQL API at localhost:5433 and the YCQL API at localhost:9042. The yb-master admin service is available at http://localhost:7000.

- To connect to the YSQL API, execute the following command:

  ```sh
  docker exec -it yb-tserver-n1 /home/yugabyte/bin/ysqlsh -h yb-tserver-n1
  ```

  ```
  ysqlsh (11.2-YB-2.0.11.0-b0)
  Type "help" for help.
  ```

  ```sh
  yugabyte=# CREATE TABLE foo(bar INT PRIMARY KEY);
  ```

- To connect to the YCQL API, execute the following command:

  ```sh
  docker exec -it yb-tserver-n1 /home/yugabyte/bin/ycqlsh yb-tserver-n1
  ```

  ```
  Connected to local cluster at yb-tserver-n1:9042.
  [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
  Use HELP for help.
  ycqlsh>
  ```

  ```sh
  ycqlsh> CREATE KEYSPACE mykeyspace;
  ycqlsh> CREATE TABLE mykeyspace.foo(bar INT PRIMARY KEY);
  ycqlsh> DESCRIBE mykeyspace.foo;
  ```

- To connect with an application client, define the client within the same Docker Compose file. The following is a sample client:

  ```sh
  version: '2'
  
  volumes:
    yb-master-data-1:
    yb-tserver-data-1:
  
  networks:
    dbinternal:
    dbnet:
  
  services:
    yb-master:
        image: yugabytedb/yugabyte:latest
        container_name: yb-master-n1
        networks:
        - dbinternal
        volumes:
        - yb-master-data-1:/mnt/master
        command: [ "/home/yugabyte/bin/yb-master",
                  "--fs_data_dirs=/mnt/master",
                  "--master_addresses=yb-master-n1:7100",
                  "--rpc_bind_addresses=yb-master-n1:7100",
                  "--replication_factor=1"]
        ports:
        - "7000:7000"
        environment:
          SERVICE_7000_NAME: yb-master
  
    yb-tserver:
        image: yugabytedb/yugabyte:latest
        container_name: yb-tserver-n1
        networks:
        - dbinternal
        - dbnet
        volumes:
        - yb-tserver-data-1:/mnt/tserver
        command: [ "/home/yugabyte/bin/yb-tserver",
                  "--fs_data_dirs=/mnt/tserver",
                  "--start_pgsql_proxy",
                  "--rpc_bind_addresses=yb-tserver-n1:9100",
                  "--tserver_master_addrs=yb-master-n1:7100"
                  ]
        ports:
        - "9042:9042"
        - "5433:5433"
        - "9000:9000"
        environment:
          SERVICE_5433_NAME: ysql
          SERVICE_9042_NAME: ycql
          SERVICE_6379_NAME: yedis
          SERVICE_9000_NAME: yb-tserver
        depends_on:
        - yb-master
  
    yb-client:
        image: yugabytedb/yb-sample-apps:latest
        container_name: yb-client-n1
        networks:
        - dbnet
        command: [ "--workload", "SqlInserts", "--nodes", "yb-tserver-n1:5433" ]
        depends_on:
        - yb-tserver
  ```

For more examples, follow the instructions in the [Quick Start](../../../quick-start/explore/ysql/#docker) section with Docker.

## Stop the cluster

You can stop the cluster using the following command:

```sh
docker-compose -f ./docker-compose.yaml down
```

## Start YugabyteDB in a single container

You can use the following Docker command to start YugabyteDB in a single container, which you might want to do during development:

```sh
docker run -d --name yugabyte -p7000:7000 -p9000:9000 -p5433:5433 -p9042:9042 -v yb_data:/home/yugabyte/var yugabytedb/yugabyte  bin/yugabyted start --daemon=false --ui=false
```

To see the status message, you can add the `-it` flag, as follows:

```sh
docker run -it --name yugabyte -p7000:7000 -p9000:9000 -p5433:5433 -p9042:9042 -v yb_data:/home/yugabyte/var yugabytedb/yugabyte  bin/yugabyted start --daemon=false --ui=false
```

