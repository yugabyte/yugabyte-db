---
title: Using DBeaver with YSQL
headerTitle: Using DBeaver
linkTitle: DBeaver
description: Use the DBeaver multi-platform database tool to explore and query YugabyteDB YSQL.
menu:
  stable:
    identifier: dbeaver-1-ysql
    parent: tools
    weight: 40
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../dbeaver-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../dbeaver-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

[DBeaver](https://dbeaver.io/) is a free [open source](https://github.com/dbeaver/dbeaver) multi-platform, cross-platform database tool for developers, SQL programmers, and database administrators. DBeaver supports various databases including PostgreSQL, MariaDB, MySQL, YugabyteDB. In addition, there are plugins and extensions for other databases that support the JDBC driver. [DBeaver Enterprise Edition](https://dbeaver.com/) supports non-JDBC data sources and allows you to explore Yugabyte YCQL tables.

![DBeaver](/images/develop/tools/dbeaver/dbeaver-view.png)

## Prerequisites

Before you can start using DBeaver with YSQL, you need to perform the following:

- Start YugabyteDB.

  For more information, see [Quick Start](../../quick-start).

- Install JRE or JDK for Java 8 or later.

  Installers can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Note that some of the installers include a JRE accessible only to DBeaver.

- Install DBeaver as follows:
  - Download the distribution package for your OS from [DBeaver Downloads](https://dbeaver.io/download/).
  - Start the installation by following instructions in [DBeaver Installation](https://github.com/dbeaver/dbeaver/wiki/Installation).

## Create a YSQL connection

You can create a connection as follows:

- Launch DBeaver.
- Navigate to **Database > New Connection** to open the **Connect to database** window shown in the following illustration.
- In the **Select your database** list, select **YugabyteDB**, and then click **Next**.\
\
    ![DBeaver Select Database](/images/develop/tools/dbeaver/dbeaver-select-db.png)

- Use **Connection Settings** to specify the following:
  - **Host**: localhost
  - **Port**: 5433
  - **Database**: replace the default value postgres with yugabyte .
  - **User**: yugabyte
  - **Password**: leave blank if YSQL authentication is not enabled. If enabled, add the password for yugabyte (default is yugabyte).
  - Select **Show all databases**.

- Click **Test Connection** to verify that the connection is successful, as shown in the following illustration:\
\
    ![DBeaver Test connection](/images/develop/tools/dbeaver/dbeaver-connected.png)

DBeaver's **Database Navigator** should display **Yugabyte - localhost**.

You can expand the list to see all databases available to the Yugabyte User, as shown in the following illustration:

![DBeaver](/images/develop/tools/dbeaver/dbeaver-localhost.png)

## What's Next

For sample databases to explore YugabyteDB using DBeaver, see [Sample datasets](/preview/sample-data/).
