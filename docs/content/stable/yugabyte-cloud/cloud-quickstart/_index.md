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

## Free Serverless Postgres

The quickest way to get started with YugabyteDB Aeon is to create a free Serverless Postgres cluster. Serverless Postgres is free up to 2 vCPU and 16 GiB of memory, and no credit card is required.

1. [Sign up](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=YBM_signup).
1. [Log in](https://cloud.yugabyte.com/login).
1. Click **Start free**.

    {{< tip title="Don't see Start free?" >}}

If you don't see the Start free option, select the **Clusters** page, then click **Explore Cluster Types**.

    {{< /tip >}}

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

### Connect to the cluster

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

### What's next

- [Manage branches and compute](../serverless/manage/) - add branches, add compute endpoints, resize compute, and more.

## Go distributed with Distributed Postgres

For multi-node, highly available YugabyteDB clusters, create a Distributed Postgres cluster. These are deployed across multiple nodes, zones, and regions, and are billed per vCPU.

- [Create a cluster](../cloud-basics/create-clusters/) - deploy single-region and multi-region clusters.

- [Request a free trial](../managed-freetrial/).

The following sections show how to connect to and explore a Distributed Postgres cluster.

### Connect using Cloud Shell

Use Cloud Shell to connect to your Distributed Postgres cluster from your browser, and interact with it using distributed SQL.

To connect to your cluster, do the following:

![Connect using cloud shell](/images/yb-cloud/cloud-connect-shell.gif)

1. On the **Clusters** page, ensure your cluster is selected.

1. Click **Connect** to display the **Connect to Cluster** dialog.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API type, and click **Confirm**.

    Cloud Shell opens in a separate browser window. Cloud Shell can take up to 30 seconds to be ready.

    ```output
    Enter your DB password:
    ```

1. Enter the password for the admin user credentials that you saved when you created the cluster.

    The shell prompt appears and is ready to use.

    ```output
    ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
    SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
    Type "help" for help.

    yugabyte=>
    ```

{{< tip title="Using Cloud Shell" >}}
Cloud Shell has a one hour connection limit. If your session is idle for more than 5 minutes, it may disconnect. If your session expires, close your browser tab and connect again.
{{< /tip >}}

The command line interface (CLI) being used is called [ysqlsh](/stable/api/ysqlsh/). ysqlsh is the CLI for interacting with YugabyteDB using the PostgreSQL-compatible [YSQL API](/stable/api/ysql/). Cloud Shell also supports [ycqlsh](/stable/api/ycqlsh/), a CLI for the [YCQL API](/stable/api/ycql/).

For information on other ways to connect to your cluster, refer to [Connect to clusters](/stable/yugabyte-cloud/cloud-connect/).

### Explore distributed SQL

When you connect to your cluster using Cloud Shell with the [YSQL API](/stable/api/ysql/) (the default), the shell window incorporates a **Quick Start Guide**, with a series of pre-built queries for you to run. Follow the prompts to explore YugabyteDB in 5 minutes.

![Run the quick start tutorial](/images/yb-cloud/cloud-shell-tutorial.gif)

### Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Aeon clusters using your favorite programming language.

Before you begin, you need the following:

- a Distributed Postgres cluster deployed in YugabyteDB Aeon.
- the cluster CA certificate; YugabyteDB Aeon uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](/stable/develop/tutorials/build-apps/cloud-add-ip/).

#### Choose your language

{{< readfile "/stable/quick-start-yugabytedb-managed/quick-start-buildapps-include.md" >}}
