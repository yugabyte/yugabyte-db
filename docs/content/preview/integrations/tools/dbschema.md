---
title: Use DbSchema with YugabyteDB YSQL
headerTitle: DbSchema
linkTitle: DbSchema
description: Use DbSchema to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: dbschema
    parent: data-tools
    weight: 60
type: docs
---

[DbSchema](https://dbschema.com/) is a visual database tool that supports over 40 databases from a single interface, and can be used to reverse-engineer schemas, edit entity-relationship (ER) diagrams, browse data, visually build queries, and synchronize schemas. This document describes how to connect DbSchema to YugabyteDB databases.

![DbSchema application](/images/develop/tools/dbschema/dbschema-application.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [Quick-start](/preview/quick-start-yugabytedb-managed/) for more info.

## Install DbSchema

1. Download the distribution package for the operating system on your client computer from the [Download DbSchema page](https://dbschema.com/download.html).
1. Install DbSchema using the install wizard.

## Create a database connection

The following steps show how to configure YugabyteDB running on your local host.

1. Launch the DbSchema application. The **Welcome to DbSchema** page appears.
1. On the **Start New Project** panel, click **Start** to connect to a database. The **Database Connection Dialog** opens.
1. In the **Alias** field, enter `YugabyteDB` to name the database connection.
1. From the **DBMS** dropdown list, select `PostgreSQL`. The **Method & Driver** field shows the available PostgreSQL JDBC drivers.
1. For the **Method & Driver** option, select **Standard (1 of 2)**. There is no need to add a driver because DbSchema includes the PostgreSQL JDBC driver.
1. In the **Compose URL** tab, click **Remote computer or custom port**. The default PostgreSQL values for server host and port appear.
1. In the port field, enter `5433` (the default port for YSQL) and then click **Check (Ping)**. A message appears saying that the YugabyteDB server is reachable.
1. Under **Authentication**, change the **Database User** to `yugabyte` (the default YugabyteDB user). If you have enabled authentication, enter the password. Otherwise, leave the field blank.
1. Under **Database**, enter `yugabyte` (the default YugabyteDB database) or the name of the database you want to connect to.
1. Click **Connect**. The **Select Schemas / Catalogs** dialog appears.
1. Click **OK** to accept the default options. Otherwise, you can customize the schema information that you want for this connection.

You have successfully created a database connection to the default database (`yugabyte`) using the default user (`yugabyte`).

## What's next

For help using DbSchema, see the [DbSchema documentation](https://dbschema.com/documentation/index.html).

YugabyteDB includes sample databases for you to explore using DbSchema. Refer to [Sample datasets](../../../sample-data/).
