---
title: Distributed Postgres quick start
headerTitle: Quick start
linkTitle: Distributed Postgres
description: Connect to a Distributed Postgres cluster in YugabyteDB Aeon and explore distributed SQL.
headcontent: Connect to a Distributed Postgres cluster and explore distributed SQL
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../" class="nav-link">
      <i class="fa-solid fa-cloud" aria-hidden="true"></i>
      Free Serverless
    </a>
  </li>
  <li>
    <a href="../distributed/" class="nav-link active">
      <i class="fa-solid fa-circle-nodes" aria-hidden="true"></i>
      Distributed
    </a>
  </li>
</ul>

For multi-node, highly available YugabyteDB clusters, create a Distributed Postgres cluster. These are deployed across multiple nodes, zones, and regions, and are billed per vCPU.

Note that before you can create a distributed cluster in YugabyteDB Aeon, you need to [add your billing profile and payment method](/stable/yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](/stable/yugabyte-cloud/managed-freetrial/).

1. If you haven't already, [Sign up](https://cloud.yugabyte.com/signup?utm_medium=direct&utm_source=docs&utm_campaign=YBM_signup) and [Log in](https://cloud.yugabyte.com/login).
1. Click **Set Up Distributed**. (If you don't see the Set Up Distributed option, select the **Clusters** page, then click **Explore Cluster Types**.)

    This displays the **Create Distributed Cluster** wizard.

1. Follow the instructions to create your distributed cluster.

    For information on the distributed topologies you can create, refer to [Create distributed clusters](/stable/yugabyte-cloud/cloud-basics/create-clusters/).

1. Click **Create Cluster**.

The distributed cluster takes up to 5 minutes to deploy.

## Connect using Cloud Shell

Use Cloud Shell to connect to your Distributed Postgres cluster from your browser, and interact with it using distributed SQL.

To connect to your cluster, do the following:

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

## Explore distributed SQL

When you connect to your cluster using Cloud Shell with the [YSQL API](/stable/api/ysql/) (the default), the shell window incorporates a **Quick Start Guide**, with a series of pre-built queries for you to run. Follow the prompts to explore YugabyteDB in 5 minutes.

## Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Aeon clusters using your favorite programming language.

Before you begin, you need the following:

- a Distributed Postgres cluster deployed in YugabyteDB Aeon.
- the cluster CA certificate; YugabyteDB Aeon uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](/stable/develop/tutorials/build-apps/cloud-add-ip/).

### Choose your language

{{< readfile "/stable/quick-start-yugabytedb-managed/quick-start-buildapps-include.md" >}}
