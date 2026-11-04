---
title: Manage a Serverless Postgres cluster in YugabyteDB Aeon
headerTitle: Manage a cluster
linkTitle: Manage a cluster
description: Add branches, add compute endpoints, and resize compute on a Serverless Postgres cluster.
headcontent: Add branches, add compute endpoints, and resize compute
menu:
  stable_yugabyte-cloud:
    identifier: aeon-serverless-manage
    parent: aeon-serverless
    weight: 20
type: docs
---

To manage a cluster, click **Clusters** in the left navigation and open the cluster. The **Cluster Branches** section lists the cluster's branches, and the compute endpoints on each branch.

## Add a branch

A branch is an isolated, zero-copy version of a database. The cluster starts with a `main` branch, and you fork other branches from it. The new branch starts with the parent's data, and writes to the branch stay on the branch. Creating a branch does not add storage cost. Additional storage is charged only when branch data changes.

A branch can have one read-write endpoint, with its own URL, vCPU, memory, trusted IP addresses, and PostgreSQL extensions.

Fork a branch when you need a separate copy of the data:

- Try a migration, schema change, or data fix, and delete the branch if you discard the result.
- Run a feature, test, or preview environment against its own copy.
- Give an agent a place to insert drafts, try updates, and create tables.
- Fork from a specific log sequence number (LSN) to work with the data as of that point.
- Start a tenant or sandbox from a template branch.

You can copy any result you want to keep from the branch back to main, then delete the branch.

Note that branches are not created with endpoints; you must add an endpoint separately. A branch can have one read-write endpoint. A branch with no endpoint keeps its data and runs no compute.

To create a branch, do the following:

1. Click **Add Branch**. To fork from a specific branch, open the actions menu (**...**) on that branch and choose **Add Child Branch**.

1. Enter a **Branch name**. Use a descriptive name, such as a feature, test, or environment name.

1. In **Fork from**, select the parent branch.

1. For **Fork point**, select **Latest state** (recommended) or **Specific LSN**, and enter the LSN to fork the parent from that point.

1. Click **Add**.

You can add child branches by opening the branch actions menu (**...**) and choosing **Add Child Branch**.

Delete a branch by opening the branch actions menu (**...**) and choosing **Delete Branch**. You can't delete the `main` branch. 

### Add extensions

Each branch can have its own PostgreSQL extensions. Extensions can't be removed after they are added, and adding one restarts every compute endpoint on the branch.

To add extension(s):

1. Open the branch actions menu (**...**) and choose **Add Extensions**.

1. Select the Extension(s) and click **Apply**.

### Edit Postgres settings

Each branch has its own [PostgreSQL configuration parameters](https://www.postgresql.org/docs/18/runtime-config.html). A change applies to that branch only.

To edit the parameters on a branch:

1. Open the branch actions menu (**...**) and choose **Advanced Settings > Edit Postgres Settings**.

1. Add PostgreSQL configuration parameters as key-value pairs.

1. Edit **Write lease settings**. `ttl_ms` is how long the branch holds the right to write, in milliseconds. `renew_seconds` is how often that right is renewed, in seconds. You can set both or neither. Both are whole numbers greater than zero, and the renewal interval must be shorter than the lease (`renew_seconds` × 1000 is less than `ttl_ms`), so a failed renewal can be retried before writes stop.

1. Click **Save**.

## Manage endpoints

A new cluster has one read-write endpoint, on the `main` branch. An endpoint consists of the connection URL, compute size, and network access settings.

A branch holds the data, and the endpoint is the PostgreSQL that clients connect to. A branch you fork has no endpoint until you add one. Add an endpoint when clients need to connect to that branch. Each endpoint has its own name in the URL, its own vCPU and memory, and its own trusted IP addresses. Branch endpoints can differ in size and access; use a small size and a few trusted addresses for your branch, while `main` uses a larger size.

### Add a compute endpoint

To add an endpoint, do the following:

1. On the branch, click **Add Compute Endpoint**.

1. On **Endpoint Settings**, set the following options and click **Next**:

    - **Role**. Choose **Read-Write**. A branch can have one read-write endpoint.
    - **Endpoint name**. The name becomes part of the URL and must be unique within the branch.
    - **Read-write compute**. Choose a size, as when you create a cluster.

1. On **Network Access**, copy the access configuration from an existing endpoint in the cluster, or start from scratch. For a public endpoint, enter the **Trusted IP Addresses**.

1. If the wizard shows **DB Credentials**, set and download the credentials.

1. Click **Add Endpoint**.

### Edit compute

Edit compute to resize an endpoint (scale vertically). Change the vCPU and memory when the workload on that branch needs more capacity, or when a smaller size is enough.

To edit the compute of an endpoint, do the following:

1. Open the endpoint actions menu (**...**) and choose **Edit Compute**.

1. Select a size: **Small**, **Medium**, **Large**, or **Custom**. The sizes follow the same rules as when you create a cluster. Memory must be at least 8 GiB per vCPU.

1. Click **Apply Resize**.

    If the new size is billed and your account has no billing profile, you are asked to create one first. If you can't manage billing, ask an account admin.

To remove the compute, click **Delete Endpoint**. Anything connected to the endpoint loses the connection.

### Change network settings

Each endpoint has its own network access settings to manage access to the endpoint.

To change access to an endpoint:

1. Open the endpoint actions menu (**...**) and choose **Advanced Settings > Network Access Settings**.

1. Choose the **Connectivity Method** (currently only Public Endpoint is available).

1. Modify the settings:

    - Public Endpoint. Provide IP addresses as a comma-separated list, or CIDR-notated range of addresses.

1. Click **Apply** when you are done.

### Edit Postgres settings

These settings apply to this endpoint only. When the branch sets the same parameter, the value on the endpoint is used.

To change endpoint Postgres settings, open the endpoint actions menu (**...**) and choose **Advanced Settings > Edit Postgres Settings**.

You can configure three processes. A serverless endpoint does not keep its own data files. Writes go to a safekeeper, which records the write-ahead log and is what the compute stays connected to. The pages are then kept by a pageserver, which serves them back to Postgres and holds the history a branch can be forked from. Change a pageserver setting when you want to change storage settings for the endpoint, such as how long page history is kept. Change a safekeeper setting when you want to customize the write-ahead log path, such as how long it waits to hear from its peers.

- **Postgres settings**

    [PostgreSQL configuration parameters](https://www.postgresql.org/docs/18/runtime-config.html), as key-value pairs. For example, `shared_buffers` and `256MB`. Most take effect immediately. A few take effect only after a restart, which disconnects anyone using this endpoint.

- **Pageserver settings**

    Storage settings for this endpoint. A dot in the name reaches a grouped setting, such as `tenant_config.pitr_interval`. A value is a number, `true`, `false`, or text such as `7 days`. `true` and `false` are booleans, and a plain number is stored as a number. Saving a pageserver setting restarts the pageserver.

- **Safekeeper settings**

    Safekeeper settings for this endpoint, such as `heartbeat_timeout` and `10s`. Names and values follow the same rules as pageserver settings. Saving a safekeeper setting restarts the safekeeper.

A parameter can be listed only once, and each row needs both a name and a value. After you click **Save**, the new value is requested. Until it is reported back, the previous value is still in effect.

