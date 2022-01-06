---
title: Hasura Cloud
headerTitle: Connect Hasura Cloud to Yugabyte Cloud
linkTitle: Hasura Cloud
description: Connect Hasura Cloud to Yugabyte Cloud.
aliases:
  - /latest/yugabyte-cloud/hasura-cloud/
menu:
  latest:
    identifier: hasura-cloud
    parent: cloud-develop
    weight: 400
isTocNested: true
showAsideToc: true
---

Use the [Hasura GraphQL Engine](https://hasura.io) with Yugabyte Cloud to power your GraphQL applications with a distributed SQL database.

This page describes how to connect a Yugabyte Cloud cluster to a Hasura Cloud project.

For an example of how to deploy a GraphQL application for a Hasura Cloud project connected to Yugabyte Cloud, refer to [Deploy a GraphQL application](../hasura-sample-app/).

## Obtain your Yugabyte cluster connection info

Sign up for Yugabyte Cloud and create a Free Tier cluster by following the steps in the cloud [Quick Start](../../cloud-quickstart).

The cluster has a default database called `yugabyte`. You'll use this along with your database credentials (username and password) in your connection with the Hasura project. To connect, you'll also need the the connection string with the cluster's host address and port number.

To get these details, in the Yugabyte Cloud Console:

1. On the **Clusters** page, select the cluster you will use for the application, and click **Connect**.

1. Click  **Connect to your Application**.

1. Select **Optimize for Hasura Cloud**.

1. Copy and record the YSQL connection string, replacing `<DB USER>` and `<DB PASSWORD>` with your cluster database credentials.

{{< warning title="Important" >}}

The connection string is a URL; be sure to encode any special characters in the hostname or password of the connection string.

{{< /warning >}}

## Create a Hasura Cloud project

To create a Hasura Cloud account, sign up at <https://cloud.hasura.io>.

For details on using Hasura Cloud, refer to the [Hasura Cloud documentation](https://hasura.io/docs/latest/graphql/cloud/index.html).

To create a project in Hasura Cloud:

1. From the Hasura Cloud Dashboard, under **Projects**, click **New Project**.

1. Select **Free Tier**, leave the default region, and enter a name for your project.

    <br/><br/>

    ![Create Hasura project](/images/deploy/yugabyte-cloud/hasura-create-project.png)

    <br/><br/>

1. Click **Create project**.

    The project details are displayed. These include the GpaphQL API endpoint URL and Admin Secret, which is used for connecting applications.

1. Note the **Hasura Cloud IP**. You will need to add this to your cluster IP allow list in Yugabyte Cloud.

## Add the Hasura Cloud project to your Yugabyte cluster IP allow list

Yugabyte Cloud restricts access to clusters to IP addresses whitelisted in IP allow lists. To connect the Hasura project, you must add the project's IP address to your clusters IP allow list.

1. In the Yugabyte Cloud console, on the **Settings** tab for your cluster, under **IP Allow lists**, click **Add List**.

1. Click **Create New List and Add to Cluster**.

1. Enter a name for the allow list (such as the name of your Hasura project) and the project IP address, and click **Save**.

## Connect the cluster to your Hasura Cloud project

1. In the Hasura Cloud Dashboard, click **Launch Console**.

    This displays the Hasura Cloud Console for the project.

1. In the Hasura Cloud Console, click the **Data** tab.

1. Under **Connect Existing Database**, enter the following details:

    * **Database Display Name**: enter a display name for your Yugabyte Cloud cluster
    * **Data Source Driver**: PostgreSQL
    * **Connect Database Via**: Database URL

1. Set the **Database URL** using your YSQL connection string. Be sure to encode any special characters in the string.

    For example

    ```url
    postgresql://admin:qwerty@1234%20.cloud.yugabyte.com:5433/yugabyte?ssl=true&sslmode=require
    ```

    \
    ![Connect Hasura database](/images/yb-cloud/hasura-cloud-connect-database.png)

1. Click **Connect Database** and wait for confirmation that the database has connected.

1. Click **View Database**. The schema is empty, and your project is ready to be [connected to an application](../hasura-sample-app/).
