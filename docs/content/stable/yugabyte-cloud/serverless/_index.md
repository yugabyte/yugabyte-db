---
title: Serverless Postgres
headerTitle: Serverless Postgres
linkTitle: Serverless Postgres
description: Create a Serverless Postgres cluster in YugabyteDB Aeon.
headcontent: Agentic Multitenant PostgreSQL, fully managed by Yugabyte
aliases:
  - /stable/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/
  - /stable/yugabyte-cloud/cloud-basics/create-clusters-free/
  - /stable/yugabyte-cloud/free-tier/
menu:
  stable_yugabyte-cloud:
    identifier: aeon-serverless
    parent: yugabytedb-managed
    weight: 15
type: docs
---

Serverless Postgres is a fully managed PostgreSQL service in YugabyteDB Aeon. It is YugabyteDB AMP (Agentic Multitenant PostgreSQL). Each agent or tenant gets its own isolated PostgreSQL database, and Yugabyte manages the service. Use it for AI agents and for workloads that are bursty or often idle.

An organization has one AMP Managed account. Choosing Serverless Postgres opens that account. Yugabyte prepares the environment and the shared pool, so the create wizard does not ask you to pick either one.

Your account includes free compute of up to 1 vCPU and 8Gi of memory, shared by everything the account runs. Any size beyond what remains is billed. Creating or resizing a cluster to a billed size asks you to create a billing profile first.

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

1. On **General**, enter a **Cluster name** and click **Next**. The name is prefilled; click the shuffle icon to get another suggestion.

1. On **Cluster Settings**, set the following and click **Next**:

    - **Engine**. **PostgreSQL** is the default. **AI Database**, **Oracle**, **SQL Server**, and **DocumentDB** are the other engines. The engine decides which extensions are included. An engine that this environment does not have is marked **Not available in this environment**.
    - **Read-write compute**. Choose a size:
        - **Free** is the free vCPU your account includes. The wizard starts on it while free vCPU remains.
        - **Small** is 2 vCPU and 16Gi. **Medium** is 4 vCPU and 32Gi. **Large** is 8 vCPU and 64Gi. If the free size matches one of these, its card is labeled with **Free**.
        - **Custom** lets you set **vCPU** and **Memory**. vCPU can be from 0.5 to 64, including millicores such as `1500m`. Memory can be from 1 to 512, in `Mi` or `Gi`, and must be at least 8Gi per vCPU.

        Sizes beyond your remaining free compute are marked **Billed**. If you select one, you are asked to create a billing profile when you click **Create Cluster**.
    - **Extensions**. Optional. What you select is seeded on the `main` branch, and every branch forked from it inherits those extensions. Extensions that the engine includes are marked **Included with engine**. You can add extensions to a branch later. You cannot remove them. For pgvector, enable it in the database with `CREATE EXTENSION vector`.

1. On **Network Access**, under **Connectivity Method**, choose how clients connect and click **Next**.

    **Public Endpoint** is the default. **Cluster-Only Access** and **Private Endpoint** are marked **Not available for AMP Managed clusters**.

    For a public endpoint, enter **Trusted IP Addresses**. Each entry is one address or a CIDR range. Separate several entries with commas.

    {{< warning title="Important" >}}

    A public endpoint with no trusted IP addresses accepts connections from anywhere on the internet. The database password is the only protection. Do not use `0.0.0.0/0` for production data.

    {{< /warning >}}

1. If the wizard shows **DB Credentials**, choose **Use default credentials** or **Add your own credentials**.

    The database user is `cloud_admin`. You set the password, not the user name. A password you enter must include lowercase and uppercase letters and meet the minimum length. Click **Download credentials**. The wizard does not continue until the file is downloaded. The credentials are only shown this once.

    {{< warning title="Important" >}}

    Save the credentials file. You need it to connect, and the console does not show the password again.

    {{< /warning >}}

1. Click **Create Cluster**.

    If you selected anything other than **Free**, and your account has no billing profile, the console asks you to create one before it creates the cluster. If you can't manage billing, ask an account admin to add a billing profile, or pick a size within your free compute.

The console reports that it is creating the cluster. The cluster then appears on **Clusters**.

## Connect

1. On **Clusters**, open the cluster.

1. On the endpoint, click **Connect**.

    **Connect** stays unavailable until the endpoint has an address.

1. If the dialog includes a CA certificate, click **Download CA Cert**.

1. Click **Copy command** and run it.

    Use `cloud_admin` as the database user, unless you created another role. The client prompts for the password from the credentials file.

    If the endpoint has a DocumentDB address, the dialog also offers **DocumentDB**. Select it and copy that command instead.

## Manage a cluster

To manage a cluster, click **Clusters** in the left navigation and open the cluster. The **Cluster Branches** section lists the cluster's branches, and the compute endpoints on each branch. Use the search box and status filter to find a branch or endpoint. The branch and endpoint counts show how many are healthy or need attention.

While a change is being applied, a banner at the top of the cluster page shows what is in progress. Some operations are unavailable until it finishes.

### Add a branch

A branch is an isolated, zero-copy version of a database. Creating a branch does not add storage cost. Additional storage is charged only when branch data changes.

1. Click **Add Branch**. To fork from a specific branch, open the actions menu on that branch and click **Add Child Branch**.

1. Enter a **Branch name**. Use a descriptive name, such as a feature, test, or environment name.

1. In **Fork from**, select the parent branch.

1. For **Fork point**, select **Latest state** (recommended) or **Specific LSN**, and enter the LSN to fork the parent from that point.

1. Click **Add**.

The branch actions menu also includes **Add Extensions**, **Edit Postgres Settings** (under **Advanced Settings**), and **Delete Branch**. You can't delete the source branch. Extensions can't be removed after they are added, and adding one restarts every compute endpoint on the branch.

### Add a compute endpoint

A branch that has no compute shows **No compute endpoint**.

1. On the branch, click **Add Compute Endpoint**.

1. On **Endpoint Settings**, set the following and click **Next**:

    - **Role**. **Read-Write** is available. **Read-Only** is not available yet. A branch can have one read-write endpoint.
    - **Endpoint name**. The name becomes part of the URL and must be unique within the branch.
    - **Read-write compute**. Choose a size, as when you create a cluster.

1. On **Network Access**, copy the access configuration from an existing endpoint in the cluster, or start from scratch. For a public endpoint, enter the **Trusted IP Addresses**.

1. If the wizard shows **DB Credentials**, set and download the credentials. The `cloud_admin` password is shared by every endpoint on the branch, so setting it here changes the credential that existing endpoints on the branch use.

1. Click **Add Endpoint**.

### Edit compute

1. On the endpoint, open the actions menu and click **Edit Compute**.

1. Select a size: **Free**, **Small**, **Medium**, **Large**, or **Custom**. The sizes follow the same rules as when you create a cluster. Memory must be at least 8Gi per vCPU.

    Free compute is shared by everything the account runs, so a size is free only within what remains. The size you are replacing doesn't count against the free compute. Sizes beyond it are marked **Billed**.

1. Click **Apply Resize**.

    If the new size is billed and your account has no billing profile, you are asked to create one first. If you can't manage billing, ask an account admin.

To remove the compute, click **Delete Endpoint**. Anything connected to the endpoint loses the connection.

## Tasks

Creating, resizing, or deleting a resource records a task. To see the tasks, click **Tasks** in the left navigation.

- Filter by **Cluster** and **Status**: **Pending**, **Running**, **Succeeded**, or **Failed**.
- Each task shows its **Task** type (for example, **Create cluster**, **Provision compute**, **Resize compute**, or **Delete branch**), **State**, **Cluster**, **Resource**, **Start time**, and **End time**.
- Click a task to open **Task details**, which includes the task ID, its progress, and the error if it failed.

## Sandbox clusters

Existing Sandbox clusters continue to work. You can't create new Sandbox clusters from the **Create Cluster** wizard; use a Serverless Postgres cluster instead.

Sandbox clusters are single-node, free clusters with the following limits:

- One Sandbox cluster per account.
- Sandbox clusters can't be scaled, and have no backups.
- Network access is limited using IP allow lists. VPC networking is not supported.
- Up to 15 simultaneous connections, and a maximum of 500 tables or 12.5 million rows. More than that may result in increased latencies, dropped connections, or out-of-memory errors.

Sandbox clusters are not suitable for proof-of-concept (POC), staging, or performance testing. [Request a free trial](../managed-freetrial/) to try out larger clusters with more resources.

Sandbox clusters are paused after 10 days of inactivity, and you receive an email notification. To resume a paused cluster, sign in to YugabyteDB Aeon, select the cluster on the **Clusters** page, and click **Resume**. A second notification is sent after 13 days of inactivity. Paused clusters are deleted after 15 days of inactivity. Idle cluster deletion jobs run daily, so your cluster may be paused or deleted up to 24 hours after the time in the notification email.

To keep a cluster from being paused, perform any of the following:

- Any SELECT, UPDATE, INSERT, or DELETE database operation.
- Create or delete tables.
- Add or remove IP allow lists.

## Next steps

- [Explore distributed SQL with Distributed Postgres](../cloud-basics/create-clusters/)
- [Build an application](/stable/develop/tutorials/build-apps/)
- [Request a free trial](../managed-freetrial/)

Share your feedback, questions, and suggestions with other users on the [YugabyteDB Community Slack]({{<slack-invite>}}).
