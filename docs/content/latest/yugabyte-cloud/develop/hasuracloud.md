---
title: Hasura Cloud
headerTitle: Connect Hasura Cloud to Yugabyte Cloud
linkTitle: Hasura Cloud
description: Connect Hasura Cloud to Yugabyte Cloud.
menu:
  latest:
    identifier: hasura-cloud
    parent: yugabyte-cloud
    weight: 700
isTocNested: true
showAsideToc: true
---

Use the [Hasura GraphQL Engine](https://hasura.io) with Yugabyte Cloud to power your GraphQL applications with a distributed SQL database.

The following example demonstrates how to deploy an application on both Hasura Cloud and Yugabyte Cloud using the Hasura sample Realtime Poll application. This application was built using React and is powered by Hasura GraphQL Engine backed by a Yugabyte Cloud YugabyteDB cluster. It has an interface for users to cast a vote on a poll and the results are updated in the on-screen bar chart in real time. 

## Prerequisites

The example has the following prerequisites.

### Yugabyte Cloud account and Free Tier cluster

Sign up for Yugabyte Cloud on the [Sign Up page](https://cloud.yugabyte.com/register).

Once registered, create a Free Tier cluster by following the steps in [Create clusters](../../create-clusters/).

The database cluster has a default database called `yugabyte` and administrator account called `admin` that we will use in our connection with our Hasura project. You will need the following cluster details:

* password
* IP address
* port number

To get these details, in the Yugabyte Cloud Console:

1. Click **Go to cluster** to display the cluster console.

1. Click **Connect**.

1. **Copy** and save the details. These are provided in the form `PGPASSWORD=password ./bin/ysqlsh -h ip_address -p port -U admin -d yugabyte`, where password, ip_address, and port correspond to the values.

### Hasura Cloud account

To create a Hasura Cloud account, sign up at <https://cloud.hasura.io>.

For details on using Hasura Cloud, see the [Hasura Cloud documentation](https://hasura.io/docs/latest/graphql/cloud/index.html).

### Hasura CLI

We will apply database migrations to our Hasura project using Hasura CLI v.2.0 Beta 2. To install Hasura CLI, refer to [Installing the Hasura CLI](https://hasura.io/docs/latest/graphql/core/hasura-cli/install-hasura-cli.html#install-hasura-cli).

Once installed, update to the v.2 Beta.

```sh
$ hasura update-cli --version v2.0.0-beta.2
```

### Realtime Poll application

The Realtime Poll application is available from the Yugabyte GraphQL Apps repo.

```sh
$ git clone https://github.com/yugabyte/yugabyte-graphql-apps
cd yugabyte-graphql-apps/realtime-poll
```

## Create a Hasura project and connect to YugabyteDB

To create a project in Hasura Cloud:

1. From the Hasura Cloud dashboard, under **Projects**, click **New Project**. 

1. Select **Free Tier**, leave the default region, and enter a project name of “yb-realtime-poll”.<br><br>

    ![Create Hasura project](/images/deploy/yugabyte-cloud/hasura-create-project.png)<br><br>

    The project details are displayed. The domain for the project is `yb-realtime-poll.hasura.app`.

1. Click **Create project**.

1. Copy and save the **Admin Secret**.

    The **Admin Secret** is used when setting up the application.

1. Click **Launch Console**.

    This displays the Hasura Cloud console for the project.

1. In the Hasura Cloud console, click the **Data** tab.

1. Under **Connect Existing Database**, enter the following details.

    * **Database Display Name**: yb-realtime-polldb
    * **Data Source Driver**: PostgreSQL
    * **Connect Database Via**: Database URL

1. Set the **Database URL** using the password, IP address, and port from your Yugabyte Cloud cluster connection info, recorded earlier.

    The Database URL is in the form `postgresql://admin:password@ip_address:port/yugabyte`.

    For example, `postgresql://admin:59jh2hj@35.217.31.152:21301/yugabyte`.<br><br>

    ![Connect Hasura database](/images/deploy/yugabyte-cloud/hasura-cloud-connect-database.png)<br><br>

1. Click **Connect Database** and wait for confirmation that the database has connected.

1. Click **View Database**. The schema will be empty.

## Set up Realtime Poll and apply migrations

On your machine, navigate to the `hasura` directory in the sample application directory.

```sh
$ cd realtime-poll/hasura
```

Edit the `config.yaml` file by changing the following parameters:

* set `endpoint` to `https://yb-realtime-poll.hasura.app`.
* set `admin_secret` to the **Admin Secret** you copied from the Hasura Cloud project dashboard.

Navigate to the `migrations` directory in the `hasura` directory.

```sh
$ cd migrations
```

Rename the `default` directory to the Database Display Name you assigned to your Yugabyte Cloud database in the Hasura project console (that is, `yb-realtime-polldb`).

```sh
$ mv default yb-realtime-polldb
```

Return to the hasura directory and apply the migration.

```sh
$ cd ..
$ hasura migrate apply --database-name yb-realtime-polldb
```

This creates the tables and views required for the polling application in the database.

## Configure the Hasura project

Return to the **Data** tab in the Hasura Cloud project console. The tables from your application are now listed under **Untracked tables or views**.

Click **Track All** and **OK** to confirm to allow the tables to be exposed over the GraphQL API.

Once the console refreshes, **Untracked foreign-key relationships** lists the relationships.

Click **Track All** and **OK** to confirm to allow the relationships to be exposed over the GraphQL API.

Add a new Array relationship for the `poll_results` table as follows:

1. Select the `poll_results` table.

1. Select the **Relationships** tab.

1. Under **Add a new relationship manually**, click **Configure**.

1. Set the following options:

    * Set **Relationship Type** to Array Relationship.
    * In the **Relationship Name** field, enter option.
    * Set **Reference Schema** to public.
    * Set **Reference Table** to option.
    * Set **From** to `poll_id` and **To** to `poll_id`.<br><br>

    ![Connect Hasura database](/images/deploy/yugabyte-cloud/hasura-cloud-relationships-add.png)<br><br>

1. Click **Save**.

## Configure Realtime Poll 

On your machine, navigate to the `src` directory in the sample application directory.

```sh
$ cd realtime-poll/src
```

Edit the `apollo.js` file by changing the following parameters:

* set the `HASURA_GRAPHQL_ENGINE_HOSTNAME` const to `yb-realtime-poll.hasura.app`.
* set the `hasura_secret` variable to the **Admin Secret** you copied from the Hasura Cloud project dashboard.

## Run Realtime Poll

To run the Realtime Poll application, navigate to the application root (`realtime-poll`) directory and run the following commands:

```sh
$ npm install 
$ npm start
```

Realtime Poll starts on http://localhost:3000.

Open a separate tab, navigate to http://localhost:3000, and cast a vote for React. The chart updates automatically using GraphQL Subscriptions.

![Connect Hasura database](/images/deploy/yugabyte-cloud/hasura-realtime-poll.png)

To verify the data being committed to the Yugabyte Cloud instance, run the following subscription query on the **API** tab of the Hasura Cloud project console:

```sh
subscription getResult($pollId: uuid!) {
  poll_results (
    order_by: {option_id:desc},
    where: { poll_id: {_eq: $pollId} }
  ) {
    option_id
    option { id text }
    votes
  }
}
```
