---
title: YugabyteDB Quick start for Aeon
headerTitle: Quick start
linkTitle: Quick start
headcontent: Get started in less than 5 minutes in the cloud or on your desktop
description: Get started using YugabyteDB in less than five minutes in the cloud on Aeon.
aliases:
  - /stable/quick-start-yugabytedb-managed/
layout: single
type: docs
rightNav:
  hideH3: true
  hideH4: true
unversioned: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../quick-start-yugabytedb-managed/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="../quick-start/macos/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Use a local cluster
    </a>
  </li>
</ul>

<!-- The steps from here to the end of "Connect to the cluster" are duplicated in yugabyte-cloud/cloud-quickstart/_index.md. Keep both in sync. -->

The quickest way to get started with YugabyteDB Aeon is to create a free **Serverless Postgres** cluster. Serverless Postgres is free up to 2 vCPU and 16 GiB of memory, and no credit card is required.

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

The first time you create a cluster, YugabyteDB Aeon creates the [Serverless account](/stable/yugabyte-cloud/managed-security/manage-accounts/) in your organization. This account is used for all your serverless clusters.

## Connect to the cluster

A new cluster has one read-write endpoint on the `main` branch. Clients can connect through a public endpoint limited by trusted IP addresses. In this case, you provided the address of your own machine when creating the free cluster.

Connect to your cluster's endpoint using psql:

1. Under the main branch **Compute Endpoints**, click **Connect**.

1. If the dialog offers **Download CA Cert**, click it and save the certificate.

1. Click **Copy** to copy the connection string. The host and port are the endpoint's address. In the copied string, do the following:

    - Replace `<DB USER>` with your database username (`cloud_admin` unless you created another role).
    - Replace `postgres` if you're connecting to a database other than the default.
    - Replace `<ROOT_CERT_PATH>` with the path to the CA certificate you downloaded. If the endpoint doesn't require the CA certificate, the string has no `sslmode` or `sslrootcert`.

    If the endpoint has a DocumentDB address, the dialog also offers **DocumentDB**. Select it and copy that command instead.

1. Run the command. When prompted, enter the password from the credentials file.

You can now run SQL against your Serverless Postgres database, and connect applications using any PostgreSQL driver.

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Serverless Postgres"
    description="Agentic Multitenant PostgreSQL for AI agents and bursty or idle workloads. Free up to 2 vCPU. Fully managed by Yugabyte."
    buttonText="Learn more"
    buttonUrl="/stable/yugabyte-cloud/serverless/"
  >}}

  {{< sections/bottom-image-box
    title="Distributed Postgres"
    description="Deploy single- and multi-region YugabyteDB clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP)."
    buttonText="Learn more"
    buttonUrl="/stable/yugabyte-cloud/distributed-postgres/"
  >}}
{{< /sections/2-boxes >}}

<!--

If you aren't using the **Get Started** tutorial, use the following instructions to create a cluster, connect to your database, explore distributed SQL, and build an application.

## Create your Sandbox cluster

The Sandbox cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required.

>**Sandbox cluster**
>
>YugabyteDB is a distributed database optimized for deployment across a cluster of servers. The Sandbox cluster has a single node and limited resources, suitable for running tutorials, [Yugabyte University](https://university.yugabyte.com), and [building sample applications](/stable/develop/tutorials/build-apps/). See [Differences between Sandbox and Dedicated clusters](/stable/faq/yugabytedb-managed-faq/#what-are-the-differences-between-sandbox-and-dedicated-clusters) for more information.
>
>To evaluate YugabyteDB Aeon for production use or conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) for trial credits.

To create your Sandbox cluster:

![Create a Sandbox cluster](/images/yb-cloud/cloud-add-free-cluster.gif)

1. Click **Create Cluster** on the welcome screen, or click **Add Cluster** on the **Clusters** page to open the **Create Cluster** wizard.

1. Select Sandbox and click **Choose**.

1. Enter a name for the cluster, choose the cloud provider (AWS or GCP), and choose the region in which to deploy the cluster, then click **Next**.

1. Click **Add Current IP Address**. The IP address of your machine is added to the IP allow list. This allows you to connect to your sandbox cluster from applications and your desktop after it is created.

1. Click **Next**.

1. Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.

1. Click **Create Cluster**.

YugabyteDB Aeon bootstraps and provisions the cluster, and configures YugabyteDB. The process takes around 5 minutes. While you wait, you can optionally fill out a survey to customize your getting started experience.

When the cluster is ready, the cluster **Overview** is displayed. You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Aeon.

## Connect to the cluster

Use Cloud Shell to connect to your YugabyteDB Aeon cluster from your browser, and interact with it using distributed SQL.

>The shell has a one hour connection limit. If your session is idle for more than 5 minutes, it may disconnect. If your session expires, close your browser tab and connect again.

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

The command line interface (CLI) being used is called [ysqlsh](/stable/api/ysqlsh/). ysqlsh is the CLI for interacting with YugabyteDB using the PostgreSQL-compatible [YSQL API](/stable/api/ysql/). Cloud Shell also supports [ycqlsh](/stable/api/ycqlsh/), a CLI for the [YCQL API](/stable/api/ycql/).

For information on other ways to connect to your cluster, refer to [Connect to clusters](/stable/yugabyte-cloud/cloud-connect/).

## Explore distributed SQL

When you connect to your cluster using Cloud Shell with the YSQL API (the default), the shell window incorporates a **Quick Start Guide**, with a series of pre-built queries for you to run. Follow the prompts to explore YugabyteDB in 5 minutes.

![Run the quick start tutorial](/images/yb-cloud/cloud-shell-tutorial.gif)

## Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). This section shows how to connect applications to YugabyteDB Aeon clusters using your favorite programming language.

Before you begin, you need the following:

- a cluster deployed in YugabyteDB Aeon.
- the cluster CA certificate; YugabyteDB Aeon uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](/stable/develop/tutorials/build-apps/cloud-add-ip/).

### Choose your language

<details><summary>Choose the language you want to use to build your application.</summary><br>
 readfile "quick-start-buildapps-include.md"
</details>

## Migrate from PostgreSQL

For PostgreSQL users seeking to transition to a modern, horizontally scalable database solution with built-in resilience, YugabyteDB offers a seamless lift-and-shift approach that ensures compatibility with PostgreSQL syntax and features while providing the scalability benefits of distributed SQL.

YugabyteDB enables midsize applications running on single-node instances to effortlessly migrate to a fully distributed database environment. As applications grow, YugabyteDB seamlessly transitions to distributed mode, allowing for massive scaling capabilities.

[YugabyteDB Voyager](/stable/yugabyte-voyager/) simplifies the end-to-end database migration process, including cluster setup, schema migration, and data migration. It supports offline and live migration from PostgreSQL to various YugabyteDB offerings, including Aeon, Anywhere, and the core open-source database.

You can [install](/stable/yugabyte-voyager/install-yb-voyager/) YugabyteDB Voyager on different operating systems such as RHEL, Ubuntu, macOS, or deploy it via Docker or Airgapped installations.

Voyager provides a powerful migration assessment that scans your existing applications and databases. This detailed assessment provides valuable insights into the readiness of your applications, data, and schema for migration.

When you're ready to migrate, you can perform an [offline migration](/stable/yugabyte-voyager/migrate/migrate-steps/); or do a non-disruptive [live migration](/stable/yugabyte-voyager/migrate/live-migrate/), with [fall-forward](/stable/yugabyte-voyager/migrate/live-fall-forward/) and [fall-back](/stable/yugabyte-voyager/migrate/live-fall-back/) capabilities.

## Learn more

[YugabyteDB Aeon Documentation](/stable/yugabyte-cloud/)

[Deploy clusters in YugabyteDB Aeon](/stable/yugabyte-cloud/cloud-basics/)

[Connect applications to YugabyteDB Aeon](/stable/yugabyte-cloud/cloud-connect/connect-applications/)

[YugabyteDB Voyager Documentation](/stable/yugabyte-voyager/)

[Explore YugabyteDB](/stable/explore/)

[Drivers and ORMS](/stable/develop/drivers-orms/)

-->
