---
title: Use Apache Superset with YugabyteDB YSQL
headerTitle: Apache Superset
linkTitle: Apache Superset
description: Use Apache Superset to explore and visulize data in YugabyteDB.
menu:
  stable:
    identifier: superset
    parent: tools
    weight: 20
type: docs
---

[Apache Superset](https://superset.apache.org/) is an open-source data exploration and visualization tool that helps you query data stored in YugabyteDB and visualize it using basic line charts to highly detailed geospatial charts.

You can use Superset to quickly explore and visualize data stored in databases and data warehouses. You can explore data without writing complex SQL queries, create rich reports and custom dashboards to visualize this data, and get insights quickly.

![Superset Dashboard](/images/develop/tools/superset/dashboard.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

Load some data to explore and visualize. For a local installation, you can load the Northwind sample database using the `./bin/yugabyted demo connect` command from your shell, or follow the [instructions](../../sample-data/northwind/).

Install the YugabyteDB psycopg2 smart driver as follows:

1. Check if the PostgreSQL psycopg2 driver is installed:

    ```sh
    pip show psycopg2
    ```

1. If present, uninstall it as follows:

    ```sh
    pip uninstall psycopg2
    ```

1. Install the YugabyteDB psycopg2 smart driver:

    ```sh
    pip install psycopg2-yugabytedb
    ```

## Install Superset

You can install Superset from scratch using [Python (pip3)](https://superset.apache.org/docs/installation/installing-superset-from-scratch) (recommended) or [Docker Compose](https://superset.apache.org/docs/installation/installing-superset-using-docker-compose).

[Launch Superset](https://superset.apache.org/docs/installation/installing-superset-from-scratch/#installing-and-initializing-superset) in your browser at `http://<hostname-or-IP-address>:8088`. If you've installed on your local computer, navigate to `localhost:8088` or `127.0.0.1:8088`. YugabyteDB v2.19 and later can also be used as a [Superset metastore](https://superset.apache.org/docs/installation/configuring-superset#using-a-production-metastore).

## Connect Superset to YugabyteDB

To connect Apache Superset to YugabyteDB:

1. Navigate to Data > Databases > + Databases, and choose **PostgreSQL** from the **Connect a database** menu.

    ![Connect Database](/images/develop/tools/superset/connect-database.png)

1. Enter your YugabyteDB tablet server's hostname or IP address with standard credentials and click **Finish**.

    ![Database Connection Credentials](/images/develop/tools/superset/connect-ybdb.png)

    {{< note title="Note" >}}
As of Docker version 18.03, the `host.docker.internal` hostname connects to your Docker host from inside a Docker container.
    {{< /note >}}

1. Verify that you can access the available databases and schemas under "Data". Navigate to Data > Datasets, and click "+Datasets".

    The dropdown list should show the databases and schemas available to explore and visualize.

    ![Loading Datasets](/images/develop/tools/superset/load-dataset.png)

You've successfully created a connection to your YugabyteDB database, and you can now start exploring and visualizing your databases using Apache Superset.

## What's next

Refer to the Apache [Superset documentation](https://superset.apache.org/docs/creating-charts-dashboards/exploring-data#exploring-data-in-superset) to learn more about Superset's data exploration capabilities. If you're creating your first dashboard using Superset, check out the [data analysis and exploration workflow](https://superset.apache.org/docs/creating-charts-dashboards/creating-your-first-dashboard/).
