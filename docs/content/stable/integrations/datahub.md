---
title: Datahub
linkTitle: Datahub
description: Use Datahub with YSQL
aliases:
menu:
  preview_integrations:
    parent: data-discovery
    identifier: datahub
    weight: 571
type: docs
---

[DataHub](https://github.com/datahub-project/datahub) is an open-source metadata platform for the data stack. DataHub is a modern data catalog built to enable end-to-end data discovery, data observability, and data governance. It supports various data sources including PostgreSQL.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, Datahub can connect to YugabyteDB as a data source using the [PostgreSQL plugin](https://datahubproject.io/docs/generated/ingestion/sources/postgres/#install-the-plugin).

## Setup

You can run the Docker Compose [quickStart example](https://github.com/datahub-project/datahub/blob/master/docker/quickstart/docker-compose-without-neo4j.quickstart.yml) provided in the Datahub GitHub repository against YugabyteDB with the following changes:

- Replace the MySql Docker image with that of YugabyteDB.
- Specify the entrypoint command for the YugabyteDB Docker container.
- Change port from 5432 to 5433
- Change username and password to yugabyte.
- Change the driver to `org.postgresql.Driver`.

Make changes in the following files:

- In `docker/quickstart/docker-compose-without-neo4j.quickstart.yml`, change the following:

  - Change the EBEAN_DATASOURCE configuration [lines 80-84 and 126-130] as follows:

    ```yaml
    EBEAN_DATASOURCE_DRIVER=org.postgresql.Driver
    EBEAN_DATASOURCE_HOST=yugabyte:5433
    EBEAN_DATASOURCE_PASSWORD=yugabyte
    EBEAN_DATASOURCE_URL=jdbc:postgresql://yugabyte:5433/yugabyte
    EBEAN_DATASOURCE_USERNAME=yugabyte
    ```

  - Change `mysql-setup` to `postgres-setup` [line 123].

  - Replace the mysql and mysql-setup container [lines 197 - 231] with yugabyte and postgres-setup container as follows:

    ```yaml
    yugabyte:
       container_name: yugabyte
       hostname: yugabyte
       image: yugabytedb/yugabyte:latest
       command: /bin/bash /home/yugabyte/docker-entrypoint-initdb.d/yb-init.sh
       environment:
         POSTGRES_USER: ${POSTGRES_USER:-yugabyte}
         POSTGRES_PASSWORD: ${POSTGRES_PASSWORD:-yugabyte}
       ports:
       - '5433:5433'
       volumes:
       - ./yb-setup/:/home/yugabyte/docker-entrypoint-initdb.d/
       healthcheck:
         test: bin/ysqlsh -h `hostname -i` -U yugabyte -tAc 'select 1' -d yugabyte
         interval: 10s
         timeout: 5s
         retries: 20
    postgres-setup:
      container_name: postgres-setup
      depends_on:
        yugabyte:
          condition: service_healthy
      environment:
      - POSTGRES_HOST=yugabyte
      - POSTGRES_PORT=5433
      - POSTGRES_USERNAME=yugabyte
      - POSTGRES_PASSWORD=yugabyte
      - DATAHUB_DB_NAME=yugabyte
      hostname: yugabyte-setup
      image: ${DATAHUB_POSTGRES_SETUP_IMAGE:-acryldata/datahub-postgres-setup}:${DATAHUB_VERSION:-head}
    ```

- Create a directory `yb-setup` in `docker/quickstart/` and a script file named `yb-init.sh` with the following content and place it under `docker/quickstart/yb-setup/` in the repository. The script runs during container initialization to launch the YugabyteDB cluster.

    ```sh
    bin/yugabyted start

    sleep 5

    bin/ysqlsh -h `hostname -i` -f /home/yugabyte/docker-entrypoint-initdb.d/init.sql
    tail -f /dev/null
    ```

- Copy the file `docker/postgres/init.sql` to `docker/quickstart/yb-setup/`.

## Run the example

Run the example using the following command:

```sh
docker compose -f docker-compose-without-neo4j.quickstart.yml up -d
```

After all the containers are running, you can ingest some demo data by running  `./datahub/docker/ingestion/ingestion.sh`, or head to <http://localhost:9002> (username: datahub, password: datahub) to access the UI.

