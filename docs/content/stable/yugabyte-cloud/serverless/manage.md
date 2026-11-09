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

A branch is an isolated, zero-copy clone of a database. The cluster starts with a `main` branch, and you fork other branches from it. The new branch starts with the parent's data, and writes to the branch stay on the branch. Creating a branch does not add storage cost. Additional storage is charged only when branch data changes.

Each branch has its own PostgreSQL extensions and settings. A branch can have one [read-write endpoint](#manage-endpoints), with its own URL, vCPU, memory, and trusted IP addresses. A new branch has no endpoint until you add one. Without an endpoint, a branch keeps its data and runs no compute.

Fork a branch when you need a separate copy of the data:

- Try a migration, schema change, or data fix, and delete the branch if you discard the result.
- Run a feature, test, or preview environment against its own copy.
- Give an agent a place to insert drafts, try updates, and create tables.
- Fork from a specific log sequence number (LSN) to work with the data as of that point.
- Start a tenant or sandbox from a template branch.

You can copy any result you want to keep from the branch back to `main`, then delete the branch.

{{< note title="Limitation" >}}
You can only fork from a parent branch that has an endpoint.
{{< /note >}}

To create a branch, do the following:

1. Click **Add Branch**. To fork from a specific branch, open the actions menu (**...**) on that branch and choose **Add Child Branch**.

1. Enter a **Branch name**. Use a descriptive name, such as a feature, test, or environment name.

1. In **Fork from**, select the parent branch.

1. For **Fork point**, select **Latest state**, or select **Specific LSN** and enter the LSN to fork the parent from that point.

1. Click **Add**.

### Add extensions

Extensions can't be removed after they are added, and adding one restarts every compute endpoint on the branch.

To add extensions to a branch:

1. Open the branch actions menu (**...**) and choose **Add Extensions**.

1. Select the extensions and click **Apply**.

### Edit branch Postgres settings

Each branch has its own [PostgreSQL configuration parameters](https://www.postgresql.org/docs/18/runtime-config.html). A change applies to that branch only.

To edit the parameters on a branch:

1. Open the branch actions menu (**...**) and choose **Advanced Settings > Edit Postgres Settings**.

1. Add PostgreSQL configuration parameters as key-value pairs.

1. Optionally, edit **Write lease settings**. `ttl_ms` is how long the branch holds the right to write, in milliseconds. `renew_seconds` is how often that right is renewed, in seconds. Set both or neither. Both are whole numbers greater than zero, and the renewal interval must be shorter than the lease (`renew_seconds` × 1000 is less than `ttl_ms`), so a failed renewal can be retried before writes stop.

1. Click **Save**.

### Delete a branch

To delete a branch, open the branch actions menu (**...**) and choose **Delete Branch**. You can't delete the `main` branch, or a branch that has child branches.

## Manage endpoints

An endpoint is the PostgreSQL that clients connect to, while the branch holds the data. An endpoint consists of the connection URL, compute size, and network access settings. A new cluster has one read-write endpoint, on the `main` branch.

Add an endpoint to a branch when clients need to connect to it. Endpoints can differ in size and access. For example, use a small size and a few trusted addresses for a test branch, and a larger size for `main`.

### Add a compute endpoint

To add an endpoint, do the following:

1. On the branch, click **Add Compute Endpoint**.

1. On **Endpoint Settings**, set the following options and click **Next**:

    - **Role**. Choose **Read-Write**. A branch can have one read-write endpoint.
    - **Endpoint name**. The name must be unique within the branch.
    - **Read-write compute**. Choose a size, as when you [create a cluster](../create-connect/#read-write-compute).

1. On **Network Access**, copy the access configuration from an existing endpoint in the cluster, or start from scratch. For a public endpoint, enter the **Trusted IP Addresses**.

1. If the wizard shows **DB Credentials**, set and download the credentials.

    {{< warning title="Credentials known issue" >}}

An endpoint added to a branch uses the password set when the cluster was created. Ignore the credentials file that Add compute offers; connect with the cluster's original password.

    {{< /warning >}}

1. Click **Add Endpoint**.

### Edit compute

Edit compute to resize an endpoint (scale vertically). Change the vCPU and memory when the workload on that branch needs more capacity, or when a smaller size is enough.

To edit the compute of an endpoint, do the following:

1. Open the endpoint actions menu (**...**) and choose **Edit Compute**.

1. Select a size. The sizes follow the same rules as when you [create a cluster](../create-connect/#read-write-compute).

1. Click **Apply Resize**.

    If the new size is billed and your account has no billing profile, you are asked to create one first. If you can't manage billing, ask an account admin.

### Change network settings

Each endpoint has its own network access settings.

To change access to an endpoint, do the following:

1. Open the endpoint actions menu (**...**) and choose **Advanced Settings > Network Access Settings**.

1. Choose the **Connectivity Method**.

1. For a public endpoint, enter the trusted IP addresses, each as one address or a CIDR range, separated by commas.

1. Click **Apply**.

### Edit endpoint settings

Endpoint settings apply to that endpoint only. When the branch sets the same PostgreSQL parameter, the value on the endpoint is used.

A serverless endpoint keeps no data files of its own. Writes go to a safekeeper, which records the write-ahead log. A pageserver then keeps the pages, serves them back to Postgres, and holds the history that a branch can be forked from. Both run alongside the endpoint, so you can tune them per endpoint.

To change endpoint settings, open the endpoint actions menu (**...**), choose **Advanced Settings > Edit Postgres Settings**, and add key-value pairs to any of the following:

- **Postgres settings**

    [PostgreSQL configuration parameters](https://www.postgresql.org/docs/18/runtime-config.html), such as `shared_buffers` and `256MB`. Most take effect immediately. A few take effect only after a restart, which disconnects anyone using this endpoint.

- **Pageserver settings**

    Storage settings, such as how long page history is kept (`tenant_config.pitr_interval`). A dot in the name reaches a grouped setting. A value is a number, `true`, `false`, or text such as `7 days`. Saving a pageserver setting restarts the pageserver.

- **Safekeeper settings**

    Write-ahead log settings, such as `heartbeat_timeout` and `10s`. Names and values follow the same rules as pageserver settings. Saving a safekeeper setting restarts the safekeeper.

A parameter can be listed only once, and each row needs both a name and a value. If the endpoint isn't running when you save, the change applies the next time it starts.

### Delete an endpoint

To remove the compute from a branch, open the endpoint actions menu (**...**) and choose **Delete Endpoint**. Clients connected to the endpoint lose their connection. The branch keeps its data.
