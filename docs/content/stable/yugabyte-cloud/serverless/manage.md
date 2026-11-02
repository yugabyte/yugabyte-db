---
title: Manage a cluster
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

Fork a branch when you need a separate copy of the data:

- Try a migration, schema change, or data fix, and delete the branch if you discard the result.
- Run a feature, test, or preview environment against its own copy.
- Give an agent a place to insert drafts, try updates, and create tables.
- Fork from a specific log sequence number (LSN) to work with the data as of that point.
- Start a tenant or sandbox from a template branch.

You can copy any result you want to keep from the branch back to main, then delete the branch.

Note that branches are not created with endpoints; you must add an endpoint separately. A branch can have one read-write endpoint. A branch with no endpoint keeps its data and runs no compute.

To create a branch, do the following:

1. Click **Add Branch**. To fork from a specific branch, open the actions menu on that branch and click **Add Child Branch**.

1. Enter a **Branch name**. Use a descriptive name, such as a feature, test, or environment name.

1. In **Fork from**, select the parent branch.

1. For **Fork point**, select **Latest state** (recommended) or **Specific LSN**, and enter the LSN to fork the parent from that point.

1. Click **Add**.

The branch actions menu also includes **Add Extensions**, **Edit Postgres Settings** (under **Advanced Settings**), and **Delete Branch**. You can't delete the `main` branch. Extensions can't be removed after they are added, and adding one restarts every compute endpoint on the branch.

## Add a compute endpoint

A new cluster has one read-write endpoint, on the `main` branch. An endpoint consists of the connection URL, compute size, and network access settings.

A branch holds the data, and the endpoint is the PostgreSQL that clients connect to. A branch you fork has no endpoint until you add one. Add an endpoint when clients need to connect to that branch. Each endpoint has its own name in the URL, its own vCPU and memory, and its own trusted IP addresses. Branch endpoints can differ in size and access; use a small size and a few trusted addresses for your branch, while `main` uses a larger size.

To add an endpoint, do the following:

1. On the branch, click **Add Compute Endpoint**.

1. On **Endpoint Settings**, set the following options and click **Next**:

    - **Role**. Choose **Read-Write**. A branch can have one read-write endpoint.
    - **Endpoint name**. The name becomes part of the URL and must be unique within the branch.
    - **Read-write compute**. Choose a size, as when you create a cluster.

1. On **Network Access**, copy the access configuration from an existing endpoint in the cluster, or start from scratch. For a public endpoint, enter the **Trusted IP Addresses**.

1. If the wizard shows **DB Credentials**, set and download the credentials. The `cloud_admin` password is shared by every endpoint on the branch, so setting it here changes the credential that existing endpoints on the branch use.

1. Click **Add Endpoint**.

## Edit compute

Edit compute to resize an endpoint (scale vertically). Change the vCPU and memory when the workload on that branch needs more capacity, or when a smaller size is enough.

To edit the compute of an endpoint, do the following:

1. On the endpoint, open the actions menu and click **Edit Compute**.

1. Select a size: **Small**, **Medium**, **Large**, or **Custom**. The sizes follow the same rules as when you create a cluster. Memory must be at least 8GB per vCPU.

    Free compute is shared by everything the account runs, so a size is free only within what remains. The size you are replacing doesn't count against the free compute. Sizes beyond it are marked **Billed**.

1. Click **Apply Resize**.

    If the new size is billed and your account has no billing profile, you are asked to create one first. If you can't manage billing, ask an account admin.

To remove the compute, click **Delete Endpoint**. Anything connected to the endpoint loses the connection.
