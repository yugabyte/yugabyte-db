---
title: Metacat
linkTitle: Metacat
description: Use Metacat with YSQL
aliases:
menu:
  preview_integrations:
    parent: data-discovery
    identifier: metacat
    weight: 571
type: docs
---

[Metacat](https://github.com/Netflix/metacat) is a metadata service for discovering, processing, and managing data. It provides a unified REST/Thrift interface to access metadata of various data stores. Metacat supports various data sources including PostgreSQL.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, Metacat can connect to YugabyteDB as a data source using the [PostgreSQL connector](https://github.com/Netflix/metacat/tree/master/metacat-connector-postgresql).

## Setup

You can run the [Docker Compose example](https://github.com/Netflix/metacat#docker-compose-example) provided in the README of the Metacat GitHub repository against YugabyteDB. To do this, you need to make the following configuration changes:

- Replace the PostgreSQL Docker image with that of YugabyteDB.
- Specify the entrypoint command for the YugabyteDB Docker container.
- Change port from 5432 to 5433.
- Change username and password to yugabyte.

Make changes in the following four files:

- Create a script file named `yb-init.sh` with the following content and place it under `metacat-functional-tests/metacat-test-cluster/datastores/postgres/docker-entrypoint-initdb.d/` in the repository. The script runs during container initialization to launch the YugabyteDB cluster.

    ```sh
    bin/yugabyted start

    sleep 5

    bin/ysqlsh -h `hostname -i` -f /docker-entrypoint-initdb.d/world/world.sql

    tail -f /dev/null
    ```

- In the `metacat-functional-tests/metacat-test-cluster/docker-compose.yml` file, change the following:

  - Change the `image` to a YugabyteDB image.
  - Add the `command` and set it to the location of the script file you created.
  - Change the `POSTGRES_USER` and `POSTGRES_PASSWORD`.
  - Change the port number under `environment` for `storage-barrier` configuration.

  For example:

    ```yaml
    postgresql:
        image: yugabytedb/yugabyte:{{<yb-version version="preview" format="build">}}
        command: /bin/bash /docker-entrypoint-initdb.d/yb-init.sh
        volumes:
            - ./datastores/postgres/docker-entrypoint-initdb.d:/docker-entrypoint-initdb.d:ro
        environment:
            - POSTGRES_USER=yugabyte
            - POSTGRES_PASSWORD=yugabyte
            - POSTGRES_DB=metacat
        labels:
          - "com.netflix.metacat.oss.test"
    ...
        storage-barrier:
            image: martin/wait:latest
            depends_on:
                - hive-metastore-db
                - postgresql
                - polaris-crdb-init
            environment:
                - TARGETS=postgresql:5433,hive-metastore-db:3306
            labels:
              - "com.netflix.metacat.oss.test"
    ```

- In the `metacat-functional-tests/metacat-test-cluster/etc-metacat/catalog/postgresql-96-db.properties` file, change the port, username, and password as follows:

    ```java
    javax.jdo.option.url=jdbc:postgresql://postgresql:5433/world
    javax.jdo.option.username=yugabyte
    javax.jdo.option.driverClassName=org.postgresql.Driver
    javax.jdo.option.password=yugabyte
    ```

- In `metacat-functional-tests/metacat-test-cluster/datastores/postgres/docker-entrypoint-initdb.d/world/world.sql`, comment out the `SET client_encoding` SQL command by prefixing it with `--`, as it is not supported in YugabyteDB:

    ```sql
    -- SET client_encoding = 'LATIN1';
    ```
