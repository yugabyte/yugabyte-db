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

This document describes how to query and visualize data in YugabyteDB using [Arctype](https://arctype.com/), a user-friendly collaborative SQL client.

Apache Superset is fast, lightweight, intuitive, data exploration and visualization tool that helps you query your data stored in YugabyteDB and visualize it using  from simple line charts to highly detailed geospatial charts.

![Arctype application](/images/develop/tools/arctype/Arctype-YB-Image-2.png)

## Before you begin

Your YugabyteDB cluster should be up and running. If you're new to YugabyteDB, create a local cluster in less than five minutes following the steps in [Quick Start](../../quick-start/install). You can also get started with the free tier of [YugabyteDB Fully-Managed Cloud](https://www.yugabyte.com/cloud/). You also need to install the Arctype client on your computer. You can download clients are available for Windows, Linux, and Mac from the [Arctype](https://arctype.com/) website.

## Create a database connection

Follow these steps to connect your Arctype desktop client to YugabyteDB:

1. Launch the Arctype desktop client.

1. Follow the in-app prompts to create and log into your Arctype account.

1. On the "Connect a Database" step, select YugabyteDB.

    ![Connect DB](/images/develop/tools/arctype/arctype-connect_step3.png)

    {{< note title="Note" >}}

If you're using YugabyteDB Cloud, you need to add your computer to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../yugabyte-cloud/cloud-secure-clusters/add-connections/). You also need to download and install CA Cert root.crt certificate on your computer from YugabyteDB Cloud console for TLS encryption.

    {{< /note >}}

1. Enter your YugabyteDB host, port, database, user, and password information, and click 'Test Connection' and save if connection is successful.

    ![Enter host and port](/images/develop/tools/arctype/arctype-connect-step4.png)

1. You can see the schemas and tables available in the YugabyteDB in the navigation panel.

    ![Enter database connection details](/images/develop/tools/arctype/arctype-connect-step5.png)

You've successfully created a connection to your YugabyteDB database, and you can now start querying and visualizing your DB using Arctype.

## What's Next

Arctype is a feature rich database query and visualization tool for developers and administrators. To learn more about these features or for help using Arctype, see the [Arctype documentation](https://docs.arctype.com/).

Check out this [blog post](https://blog.yugabyte.com/yugabytedb-arctype-sql-integration/) to learn more about deep integration between YugabyteDB and Arctype.

YugabyteDB has several sample databases available for you to explore. To learn more about the available sample databases, see [Sample data](../../sample-data/).
