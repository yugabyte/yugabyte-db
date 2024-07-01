---
title: Use pgAdmin with YugabyteDB YSQL
headerTitle: pgAdmin
linkTitle: pgAdmin
description: Administer and manage YugabyteDB distributed SQL databases using pgAdmin.
menu:
  v2.16:
    identifier: pgadmin
    parent: tools
    weight: 10
type: docs
---

[pgAdmin](https://pgadmin.org) is a popular open source administration and management tool for PostgreSQL databases. It simplifies the creation, maintenance, and use of database objects. PgAdmin includes a connection wizard, built-in SQL editor to import SQL scripts, and a mechanism to auto-generate SQL scripts if you need to run them on the database command line shell. You can run PgAdmin through the web interface, or as a downloadable application that is locally installed. Because YugabyteDB is PostgreSQL-compatible, you can also use pgAdmin to work with YugabyteDB.

## Before you begin

To use pgAdmin with YugabyteDB, you need to have YugabyteDB up and running, the required Java Runtime Environment, and the required PostgreSQL JDBC driver.

### YugabyteDB

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

### PostgreSQL JDBC driver

To connect pgAdmin to a YugabyteDB cluster, you need the PostgreSQL JDBC driver installed. To download the current version that supports Java 8 or later, go to the [PostgreSQL JDBC Driver download](https://jdbc.postgresql.org/download/) page.

## Install pgAdmin

To install pgAdmin, go to the [Download page](https://www.pgadmin.org/download/) and select the version of pgAdmin 4 for your operating system.

## Configure pgAdmin

Add a pgAdmin server to connect to a cluster as follows:

1. Launch the pgAdmin 4 application. You are prompted to save a master password for the application.

1. Under Quick Links, click **Add New Server** to display the **Register - Server** window.

1. On the **General** tab, enter a name for your server, such as YugabyteDB.

1. On the **Connection** tab, fill in the [connection parameters](../#connection-parameters).

1. For YugabyteDB Managed clusters, on the **SSL** tab, set **Root certificate** to the cluster root certificate you downloaded.

1. Click **Save**. The new connection appears in the application.

Expand **Databases** to see a list of all available databases.

![Available databases](/images/develop/tools/pgadmin/pgadmin-list-of-databases.png)

You can begin exploring YugabyteDB databases.

## What's next

For details on using pgAdmin, click **Help** in the pgAdmin menu.

If you're looking for sample databases to explore YugabyteDB using pgAdmin, refer to [Sample datasets](../../sample-data/).
