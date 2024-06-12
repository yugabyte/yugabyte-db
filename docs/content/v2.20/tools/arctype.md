---
title: Use Arctype with YugabyteDB YSQL
headerTitle: Arctype
linkTitle: Arctype
description: Use Arctype to work with distributed SQL databases in YugabyteDB.
menu:
  v2.20:
    identifier: arctype
    parent: tools
    weight: 30
type: docs
---

[Arctype](https://arctype.com/) is a collaborative SQL database client that is free to use and cross platform. It offers one-click query sharing for teams, and you can visualize query output and combine multiple charts and tables into a dashboard.

Arctype also features [integrated support](https://docs.arctype.com/connect/yugabytedb) for connecting to YugabyteDB clusters.

This document describes how to connect to YugabyteDB using Arctype.

![Arctype application](/images/develop/tools/arctype/Arctype-YB-Image-2.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../#yugabytedb-prerequisites).

## Install Arctype

Download clients for Windows, Linux, and Mac from the [Arctype](https://arctype.com/) website.

## Create a database connection

Follow these steps to connect your Arctype desktop client to YugabyteDB:

1. Launch the Arctype desktop client.

1. Follow the in-app prompts to create and log into your Arctype account.

1. On the "Connect a Database" step, select YugabyteDB.

    ![Connect YugabyteDB](/images/develop/tools/arctype/arctype-connect_step3.png)

1. Enter your YugabyteDB [connection parameters](../#connection-parameters).

1. Click **Test Connection** and, if the connection is successful, click **Save**.

    ![Enter connection parameters](/images/develop/tools/arctype/arctype-connect-step4.png)

You can see the schemas and tables available in the YugabyteDB in the navigation panel.

![YugabyteDB database connection](/images/develop/tools/arctype/arctype-connect-step5.png)

You've successfully created a connection to your YugabyteDB database, and you can now start querying and visualizing your DB using Arctype.

## What's Next

To learn more about Arctype, refer to the [Arctype documentation](https://docs.arctype.com/).

To learn about Arctype integration with Yugabyte, refer to the [YugabyteDB Integrates with Arctype SQL Client](https://www.yugabyte.com/blog/yugabytedb-arctype-sql-integration/) blog post and [YugabyteDB](https://docs.arctype.com/connect/yugabytedb/) in the Arctype documentation.

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](../../sample-data/).
