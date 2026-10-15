---
title: Serverless Postgres
headerTitle: Serverless Postgres
linkTitle: Serverless Postgres
description: Create a Serverless Postgres cluster in YugabyteDB Aeon.
headcontent: Agentic Multitenant PostgreSQL, fully managed by Yugabyte
aliases:
  - /stable/amp/
menu:
  stable_yugabyte-cloud:
    identifier: aeon-serverless
    parent: yugabytedb-managed
    weight: 15
type: docs
---

Serverless Postgres is a fully managed PostgreSQL service in YugabyteDB Aeon. It is YugabyteDB AMP (Agentic Multitenant PostgreSQL). Each agent or tenant gets its own isolated PostgreSQL database, and Yugabyte manages the service. Use it for AI agents and for workloads that are bursty or often idle.

An organization has one AMP Managed account. Choosing Serverless Postgres opens that account. Yugabyte prepares the environment and the shared pool, so the create wizard does not ask you to pick either one.

Your account includes an amount of free vCPU. The cluster type card shows how much. A size above what remains is billed, and creating that cluster asks for a billing profile first.

## Features

A Serverless Postgres cluster includes the following:

- Scale to zero.
- An isolated PostgreSQL database for each agent or tenant.
- Zero-copy branching.
- A path to a distributed YugabyteDB cluster without a migration.
- Built-in connection pooling.
- Fair CPU sharing across tenants.

## Create a cluster

To create a Serverless Postgres cluster:

1. On the **Clusters** page, click **Add Cluster**.

1. On **Choose a cluster type**, select **Serverless Postgres** and click **Choose**.

    If the environment is still being prepared, the wizard shows **Before you proceed ...** and you wait until it is serving.

1. On **General**, enter a **Cluster name** and click **Next**.

1. On **Cluster Settings**, set the following and click **Next**:

    - **Engine**. **PostgreSQL** is the default. **AI Database**, **Oracle**, **SQL Server**, and **DocumentDB** are the other engines. The engine decides which extensions are included. An engine that this environment does not have is marked **Not available in this environment**.
    - **Extensions**. Optional. What you select is seeded on the `main` branch, and every branch forked from it inherits those extensions. You can add extensions to a branch later. You cannot remove them. For pgvector, enable it in the database with `CREATE EXTENSION vector`.
    - **Read-write compute**. **Small** is 2 vCPU and 16Gi. **Medium** is 4 vCPU and 32Gi. **Large** is 8 vCPU and 64Gi. **Custom** lets you set **vCPU** and **Memory**. vCPU can be from 0.5 to 64, including millicores such as `1500m`. Memory can be from 1 to 512, in `Mi` or `Gi`.

1. On **Network Access**, under **Connectivity Method**, choose how clients connect and click **Next**.

    **Public Endpoint** is the default. **Cluster-Only Access** and **Private Endpoint** are marked **Not available for AMP Managed clusters**.

    For a public endpoint, enter **Trusted IP Addresses**. Each entry is one address or a CIDR range. Separate several entries with commas.

    {{< warning title="Important" >}}

A public endpoint with no trusted IP addresses accepts connections from anywhere on the internet. The database password is the only protection. Do not use `0.0.0.0/0` for production data.

    {{< /warning >}}

1. If the wizard shows **DB Credentials**, choose **Use default credentials** or **Add your own credentials**.

    The database user is `cloud_admin`. You set the password, not the user name. Click **Download credentials**. The wizard does not continue until the file is downloaded. The credentials are only shown this once.

    {{< warning title="Important" >}}

Save the credentials file. You need it to connect, and the console does not show the password again.

    {{< /warning >}}

1. Click **Create Cluster**.

The console reports that it is creating the cluster. The cluster then appears on **Clusters**.

## Connect

1. On **Clusters**, open the cluster.

1. On the endpoint, click **Connect**.

    **Connect** stays unavailable until the endpoint has an address.

1. If the dialog includes a CA certificate, download it.

1. Click **Copy command** and run it.

    Use `cloud_admin` as the database user, unless you created another role. The client prompts for the password from the credentials file.

    If the endpoint has a DocumentDB address, the dialog also offers **DocumentDB**. Select it and copy that command instead.
