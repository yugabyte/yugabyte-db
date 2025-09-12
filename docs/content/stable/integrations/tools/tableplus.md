---
title: Use TablePlus with YugabyteDB YSQL and YCQL
headerTitle: TablePlus
linkTitle: TablePlus
description: Learn how to connect TablePlus to YugabyteDB and query using YSQL, YCQL.
aliases:
  - /develop/tools/tableplus/
  - /preview/develop/tools/tableplus/
  - /preview/tools/tableplus/
menu:
  preview_integrations:
    identifier: tableplus
    parent: tools
    weight: 95
type: docs
---

[TablePlus](https://tableplus.io/) is a popular database developer console with built-in integrations with major databases including PostgreSQL, Cassandra, and Redis. It is free to get started with the option of upgrading to a [perpetual paid license](https://tableplus.io/pricing). TablePlus works with YugabyteDB without any issues because the YugabyteDB APIs are compatible at the wire protocol level with databases already supported by TablePlus.

This tutorial shows how to connect TablePlus to a YugabyteDB cluster.

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

Note that if you are connecting to a YCQL keyspace that has tables with columns of type JSONB, because the Cassandra driver used by TablePlus does not support JSONB, the connection to the database may fail.

## Install TablePlus

To install TablePlus, go to the [Download page](https://tableplus.com/download/) and select the version for your operating system.

## Create connections

You can use TablePlus to connect to YugabyteDB using the YSQL and YCQL APIs.

To create a connection, do the following:

1. In TablePlus, from the **Connection** menu, choose **New**.

1. Select **PostgreSQL** for YSQL, or **Cassandra** for YCQL, and click **Create**.

    ![Choose DB](/images/develop/tools/tableplus/choose-db.png)

1. Enter a **Name** for the connection and fill in the [connection parameters](../#connection-parameters).

1. Click **Test** to verify that TablePlus can connect with YugabyteDB. The color of the fields changes to green if the test succeeds.

    ![YSQL connection parameters](/images/develop/tools/tableplus/ysql-connection.png)

1. Click **Connect** to create the connection.

Connections are saved in the application, and displayed every time you start TablePlus.

![YB ALL](/images/develop/tools/tableplus/yb-all-connection.png)

## What's next

To get started with TablePlus, follow the instructions in [Getting Started with TablePlus](https://tableplus.io/blog/2018/04/getting-started-with-tableplus.html).

