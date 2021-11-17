---
title: Hasura Cloud
headerTitle: Connect Hasura Cloud to Yugabyte Cloud
linkTitle: Hasura Cloud
description: Connect Hasura Cloud to Yugabyte Cloud.
menu:
  v2.6:
    identifier: hasura-cloud
    parent: yugabyte-cloud
    weight: 700
isTocNested: true
showAsideToc: true
---

Use the [Hasura GraphQL Engine](https://hasura.io) with Yugabyte Cloud to power your GraphQL applications with a distributed SQL database.

This page describes how to connect a Yugabyte Cloud cluster to a Hasura project.

## Obtain your Yugabyte cluster connection info

Sign up for Yugabyte Cloud on the [Sign Up page](https://cloud.yugabyte.com/register).

Once registered, create a Free Tier cluster by following the steps in [Create clusters](../../create-clusters/).

The cluster has a default database called `yugabyte`, and an administrator account called `admin`. You'll use these in your connection with the Hasura project. To connect, you'll need the `admin` account's password, and the cluster's IP address and port.

To get these details, in the Yugabyte Cloud Console:

1. Click **Go to cluster** to display the cluster console.

1. Click **Connect**.

1. **Copy** and save the details. These are provided in the form 

    ```output
    PGPASSWORD=password ./bin/ysqlsh -h ip_address -p port -U admin -d yugabyte
    ```

    where password, ip_address, and port correspond to the admin password, IP address, and port.

## Connect the cluster to your Hasura Cloud project

To create a Hasura Cloud account, sign up at <https://cloud.hasura.io>.

For details on using Hasura Cloud, refer to the [Hasura Cloud documentation](https://hasura.io/docs/latest/graphql/cloud/index.html).

To create a project in Hasura Cloud:

1. From the Hasura Cloud dashboard, under **Projects**, click **New Project**. 

1. Select **Free Tier**, leave the default region, and enter a name for your project.

    <br/><br/>

    ![Create Hasura project](/images/deploy/yugabyte-cloud/hasura-create-project.png)
    
    <br/><br/>

1. Click **Create project**.

    The project details are displayed. These include the GpaphQL API endpoint URL and Admin Secret, which is used for connecting applications.

1. Click **Launch Console**.

    This displays the Hasura Cloud console for the project.

1. In the Hasura Cloud console, click the **Data** tab.

1. Under **Connect Existing Database**, enter the following details:

    * **Database Display Name**: enter a name for your Yugabyte cluster
    * **Data Source Driver**: PostgreSQL
    * **Connect Database Via**: Database URL

1. Set the **Database URL** using the password, IP address, and port from your Yugabyte Cloud cluster connection info, recorded earlier.

    The Database URL is in the form `postgresql://admin:password@ip_address:port/yugabyte`.

    For example, `postgresql://admin:qwerty@12.345.67.890:21301/yugabyte`.
    
    <br/><br/>

    ![Connect Hasura database](/images/deploy/yugabyte-cloud/hasura-cloud-connect-database.png)

1. Click **Connect Database** and wait for confirmation that the database has connected.

1. Click **View Database**. The schema is empty, and your project is ready to be [connected to an application](../hasura-sample-app/).
