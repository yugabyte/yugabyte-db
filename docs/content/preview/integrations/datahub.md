---
title: Datahub
linkTitle: Datahub
description: Use Datahub with YSQL
aliases:
menu:
  preview_integrations:
    parent: integrations
    identifier: datahub
    weight: 571
type: docs
---

[DataHub](https://github.com/datahub-project/datahub) is an open-source metadata platform for the modern data stack. DataHub is a modern data catalog built to enable end-to-end data discovery, data observability, and data governance. It supports various data sources including PostgreSQL.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, Datahub can connect to YugabyteDB as a data source using the [PostgreSQL plugin](https://datahubproject.io/docs/generated/ingestion/sources/postgres/#install-the-plugin).

## Setup

You can run the Docker Compose [quickStart example](https://github.com/datahub-project/datahub/blob/master/docker/quickstart/docker-compose-without-neo4j.quickstart.yml) provided in the Datahub's GitHub repository against YugabyteDB with the following changes:

- Replace the MySql Docker image with that of YugabyteDB.
- Specify entrypoint command for the YugabyteDB Docker container.
- Change port from 5432 to 5433
- Change username and password to yugabyte.
- Change the driver to `org.postgresql.Driver`.

Make changes in the following four files:

- Create a directory `yb-setup` in `docker/quickstart/` and a script file named `yb-init.sh` with the following content and place it under `docker/quickstart/yb-setup/` in the repository. The script runs during container initialization to launch the YugabyteDB cluster.

    ```sh
    bin/yugabyted start

    sleep 5

    bin/ysqlsh -h `hostname -i` -f /home/yugabyte/docker-entrypoint-initdb.d/init.sql
    tail -f /dev/null
    ```

- Copy the file `docker/postgres/init.sql` to `docker/quickstart/yb-setup/`.
