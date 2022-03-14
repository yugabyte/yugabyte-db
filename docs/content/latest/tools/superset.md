---
title: Use Apache Superset with YugabyteDB YSQL
headerTitle: Apache Superset
linkTitle: Apache Superset
description: Use Apache Superset to explore and visulize data in YugabyteDB.
menu:
  latest:
    identifier: superset
    parent: tools
    weight: 2794
isTocNested: true
showAsideToc: true
---

[Apache Superset](https://superset.apache.org/) is a fast, lightweight and intuitive open-source data exploration and visualization tool that helps you query your data stored in YugabyteDB and visualize it using simple line charts to highly detailed geospatial charts.

![Superset Dashboard](/images/develop/tools/superset/dashboard.png)

## Before you begin

Your YugabyteDB cluster should be up and running. If you're new to YugabyteDB, create a local cluster in less than five minutes following the steps in [Quick Start](../../quick-start/install). You can also get started with the free tier of [YugabyteDB Fully-Managed Cloud](https://www.yugabyte.com/cloud/). You also need to load a relevant database in your YugabyteDB for exploration and visualizaation. If using yugabyted, you can load the Northwind sample database with the ./bin/yugabyted demo connect command from your shell or you can follow [instructions here](https://docs.yugabyte.com/latest/sample-data/northwind/).

You also need to install Apache Superset to explore and visualize your data. You can install Superset using [Docker Compose](https://superset.apache.org/docs/installation/installing-superset-using-docker-compose) (recommended) or from scratch using [python install (PIP)](https://superset.apache.org/docs/installation/installing-superset-from-scratch).

## Connecting Apache Superset with YugabyteDB

After successful installation, launch Superset in the browser at http://<hostname/IP>:8088. In case of local installation on your laptop, navigate to localhost:8088 or 127.0.0.1:8088. Superset comes with standard PostgreSQL driver that also connects to YugabyteDB. You can also manually install [psycopg2 driver](https://www.psycopg.org/docs/) to connect to YugabyteDB.

Follow these steps to connect Apache Superset to YugabyteDB:

1. Navigate to Data → Databases → + Databases and choose "PostgreSQL" from the “Connect a database” menu as shown below.

![Connect Database](/images/develop/tools/superset/connect-database.png)

2. Enter the hostname or IP Address of your YB-TServer with standard credentials and click "Finish".

![Database Connection Credentials](/images/develop/tools/superset/connect-ybdb.png)

{{< note title="Note" >}}
As of Docker version 18.03, the host.docker.internal hostname connects to your Docker host from inside a Docker container.
{{< /note >}}

3. You should now be connected to the YugabyteDB and to verify you can check the availabe databases and schemas under "Data". Navigate to Data → Datasets and click "+Datasets" and in the dialog configure as shown below. Dropdown selectors would show the datasbes and schemas available for explore and visualize operations.

    ![Loading Datasets](/images/develop/tools/superset/load-dataset.png)

You've successfully created a connection to your YugabyteDB database, and you can now start exploring and visualizing your databases using Apache Superset.

## What's Next

Superset is a rich, open-source data exploration and visualization platform that allows Business Analysts, Data Scientists and Developers to quickly explore and visualize data stored in their databases and data warehouses. You can explore data without writing complex SQL queries, create rich reports and custom dashboards to visualize this data and get insights quickly.

Refer to Apache Superset [documentation](https://superset.apache.org/docs/creating-charts-dashboards/exploring-data#exploring-data-in-superset) to learn more about Superset's data exploration capabilities. Also, in case you are creating your first dashboard using Superset then check out [instructions for data analysis and exploration workflow](https://superset.apache.org/docs/creating-charts-dashboards/creating-your-first-dashboard/).
