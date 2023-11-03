---
title: Using DBeaver with YugabyteDB YCQL
headerTitle: Using DBeaver with YugabyteDB YCQL
linkTitle: DBeaver
description: Use the DBeaver multi-platform database tool to explore and query YugabyteDB.
menu:
  v2.12:
    identifier: dbeaver-ycql
    parent: tools
    weight: 2711
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../dbeaver-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../dbeaver-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

[DBeaver](https://dbeaver.io/) is a free [open source](https://github.com/dbeaver/dbeaver) multi-platform, cross-platform database tool for developers, SQL programmers, and database administrators. DBeaver supports various databases including PostgreSQL, MariaDB, MySQL, YugabyteDB. In addition, there are plugins and extensions for other databases that support the JDBC driver. [DBeaver Enterprise Edition](https://dbeaver.com/) supports non-JDBC data sources and allows you to explore Yugabyte YCQL tables.

![img](/images/develop/tools/dbeaver/dbeaver-view.png)

## Prerequisites

Before you can start using DBeaver with YCQL, you need to perform the following:

- Start YugabyteDB.

  For more information, see [Quick Start](/quick-start/install).

- Install JRE or JDK for Java 8 or later.

  Installers can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Note that some of the installers include a JRE accessible only to DBeaver.

- Install [DBeaver Enterprise Edition](https://dbeaver.com/download/enterprise/).


## Creating a YugabyteDB YCQL Connection

You can create a connection as follows:

- Launch DBeaver.
- Navigate to **Database > New Connection** to open the **Connect to database** window shown in the following illustration.
- In the **Select your databas**e list, select **NoSQL > Yugabyte CQL**, and then click **Next**.

![img](/images/develop/tools/dbeaver/dbeaver-select-db-ycql.png)

- Use **Connection Settings** to specify the following:
  - **Host**: localhost
  - **Port**: 9042
  - **Keyspace**: system
  - **User**: leave blank if YCQL authentication is not enabled. If enabled, enter username.
  - **Password**: leave blank if YCQL authentication is not enabled. If enabled, enter the password.
  - Select **Show all databases**.

- Click **Test Connection** to verify that the connection is successful, as shown in the following illustration:

![img](/images/develop/tools/dbeaver/dbeaver-test-conn-ycql.png)

DBeaver's **Database Navigator** should display system.

You can expand the list to see all keyspaces available in YugabyteDB cluster, as shown in the following illustration:

![img](/images/develop/tools/dbeaver/dbeaver-ycql-system.png)

For sample databases to explore YugabyteDB CQL using DBeaver, see [Sample data](/preview/explore/json-support/jsonb-ycql/#root).
