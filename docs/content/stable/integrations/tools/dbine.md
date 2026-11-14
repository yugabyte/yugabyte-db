---
title: Use DBine with YugabyteDB YSQL
headerTitle: DBine
linkTitle: DBine
description: Use DBine to explore and query YugabyteDB YSQL.
menu:
  stable_integrations:
    identifier: dbine
    parent: tools
    weight: 55
type: docs
---

[DBine](https://dbine.com) is a free, open source (Apache 2.0 licensed) desktop database manager for Windows, macOS, and Linux, which works with 130+ databases and cloud services in the same window. DBine has a YugabyteDB connection type that connects to YugabyteDB YSQL using the PostgreSQL wire protocol.

![DBine query editor connected to YugabyteDB](/images/develop/tools/dbine/dbine-query-editor.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

## Install DBine

Download and install DBine from the [releases page](https://github.com/addlayer-io/dbine/releases).

## Create a connection

To connect DBine to a YugabyteDB cluster:

1. In DBine, click **New connection**.
1. Select **YugabyteDB** from the list of engines.
1. Fill in the [connection parameters](../#connection-parameters). The port defaults to 5433 for YSQL.
1. For YugabyteDB Aeon clusters, open the **SSL/TLS** tab, select **Encrypt the connection (TLS)**, and in **CA certificate (PEM)** select the downloaded `root.crt`; Aeon requires TLS.
1. Click **Test connection** to verify, then **Save and connect**.

The connection appears in the **Explorer**. You can browse databases, schemas, tables, views, columns, and indexes, and right-click a database and choose **New query** to run SQL and view the results. Right-click the connection and choose **Monitor** to see the cluster's nodes, sessions, and database sizes.

## What's next

For details on using DBine, refer to the [DBine repository](https://github.com/addlayer-io/dbine).

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](/stable/develop/sample-data/).
