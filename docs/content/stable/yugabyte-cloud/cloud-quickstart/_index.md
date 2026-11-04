---
title: YugabyteDB Aeon quick start
headerTitle: Quick start
linkTitle: Quick start
headcontent: Sign up for YugabyteDB Aeon and create a free Serverless Postgres cluster
description: Get started using YugabyteDB Aeon in less than five minutes.
layout: single
type: docs
menu:
  stable_yugabyte-cloud:
    parent: yugabytedb-managed
    weight: 2
    params:
      hideLink: true
rightNav:
  hideH3: true
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../cloud-quickstart/" class="nav-link active">
      <i class="fa-solid fa-cloud" aria-hidden="true"></i>
      Free Serverless
    </a>
  </li>
  <li>
    <a href="distributed/" class="nav-link">
      <i class="fa-solid fa-circle-nodes" aria-hidden="true"></i>
      Distributed
    </a>
  </li>
</ul>

The quickest way to get started with YugabyteDB Aeon is to create a free Serverless Postgres cluster. Serverless Postgres is free up to 2 vCPU and 16 GiB of memory, and no credit card is required.

1. [Sign up](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=YBM_signup).
1. [Log in](https://cloud.yugabyte.com/login).
1. Click **Start free**. (If you don't see the Start free option, select the **Clusters** page, then click **Explore Cluster Types**.)

    This displays the **Create Postgres Cluster** dialog with settings already selected:

    - Cluster name
    - Engine, region, and compute
    - Your IP address added as a trusted IP so that you will be able to connect to the cluster from your machine
    - Database credentials.

1. Click **Download Credentials** to download the credentials file to your machine. You will use these to connect to your database.
1. Click **Create Cluster**.

    When finished, your cluster is listed on the **Clusters** page.

1. Select the cluster to open it.

![Free serverless cluster](/images/yb-cloud/serverless-free.png)

The first time you create a cluster, YugabyteDB Aeon creates the [Serverless account](../managed-security/manage-accounts/) in your organization. This account is used for all your serverless clusters.

## Connect to the cluster

A new cluster has one read-write endpoint on main. Clients can connect through a public endpoint limited by trusted IP addresses. In this case, you provided the address of your own machine when creating the free cluster.

Connect to your cluster's endpoint using psql:

1. Under the main branch **Compute Endpoints**, click **Connect**.

1. Click **Download CA Cert**.

1. Click **Copy** to copy the connection string:

    - The host address is populated with the address for the endpoint you selected. This is also shown under **Connection Details** for the endpoint.
    - port is 55433.
    - Replace `<DB USER>` with your database username (`cloud_admin` unless you created another role).
    - `postgres` is the default database; replace it if you're connecting to a database other than the default.
    - Replace `<ROOT_CERT_PATH>` with the path to the root certificate you downloaded to your computer.

    If the endpoint has a DocumentDB address, the dialog also offers **DocumentDB**. Select it and copy that command instead.

1. Run the command in your terminal.

    The client prompts for the password from the credentials file when you run the command.

1. Enter your database password.

You can now run SQL against your Serverless Postgres database, and connect applications using any PostgreSQL driver.

## What's next

- [Manage branches and compute](../serverless/manage/) - add branches, add compute endpoints, resize compute, and more.
