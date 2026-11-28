---
title: Use Draxlr with YugabyteDB YSQL
headerTitle: Draxlr
linkTitle: Draxlr
description: Use Draxlr to explore YugabyteDB YSQL data and build dashboards.
menu:
  stable_integrations:
    identifier: draxlr
    parent: tools
    weight: 62
type: docs
---

[Draxlr](https://www.draxlr.com/databases/yugabytedb/) is a hosted business intelligence tool. You can browse and filter tables, build queries and dashboards, ask questions about your data with AI, set up automations, and embed dashboards in your own application. Draxlr has a built-in YugabyteDB connector that uses the YSQL API.

![Draxlr dashboard with charts, a pivot table, and filters](/images/develop/tools/draxlr/draxlr-dashboard.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

Draxlr runs in the cloud, so your cluster must accept connections from Draxlr's IP address, `54.205.255.226`:

- For YugabyteDB Aeon clusters, [add the address to the cluster IP allow list](/stable/yugabyte-cloud/cloud-secure-clusters/add-connections/).
- For self-managed clusters, allow the address on port 5433 in your firewall, or connect through an SSH tunnel.

A cluster that listens only on `localhost` is not reachable from Draxlr.

Draxlr only runs read queries, so connect with a read-only database user rather than an administrator.

## Create a connection

Sign up at [app.draxlr.com](https://app.draxlr.com). There is nothing to install.

To connect Draxlr to a YugabyteDB cluster:

1. On the **Add Database** page, click **Connect Database**.
1. Under **Select Data Source**, select **YugabyteDB**.
1. Under **Database Credentials**, enter a **Connection Name**, and fill in **Host**, **Port**, **User**, **Password**, and **Database** using the [connection parameters](../#connection-parameters). Use port 5433 for YSQL, not the PostgreSQL default of 5432.
1. To connect through a bastion host, turn on **Connect with SSH** and enter the SSH host, port, user, and key file.
1. Click **Next**. Draxlr tests the connection.
1. Under **Select Schemas**, choose the schemas to use and click **Next**.

Draxlr syncs the tables and columns of the selected schemas. You can then browse tables, build queries and dashboards, and ask questions with AI.

## What's next

For details on using Draxlr, refer to the [Draxlr documentation](https://docs.draxlr.com/docs/database-credentials/yugabyte).

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](/stable/develop/sample-data/).
