---
title: Serverless Postgres in YugabyteDB Aeon
headerTitle: Serverless Postgres
linkTitle: Serverless Postgres
headContent: Agentic multitenant PostgreSQL, fully managed by Yugabyte
description: Serverless Postgres in YugabyteDB Aeon for AI agents and bursty or idle workloads.
aliases:
  - /stable/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/
  - /stable/yugabyte-cloud/cloud-basics/create-clusters-free/
  - /stable/yugabyte-cloud/free-tier/
menu:
  stable_yugabyte-cloud:
    identifier: aeon-serverless
    parent: yugabytedb-managed
    weight: 24
    params:
      classes: separator
type: indexpage
showRightNav: true
---

Serverless Postgres is a fully managed PostgreSQL service in YugabyteDB Aeon. Each agent or tenant gets its own isolated PostgreSQL database. Use it for AI agents and for workloads that are bursty or often idle.

A Serverless Postgres cluster includes the following features:

- An isolated PostgreSQL database for each agent or tenant.
- Zero-copy branching.
- Resource isolation between tenants.

Your organization has a single account for serverless clusters, created when you create your first Serverless Postgres cluster. YugabyteDB Aeon prepares the environment and the shared pool for you.

Serverless Postgres is free to start, and includes 2 vCPU, 16 GiB RAM, and 100 GiB storage.

This allowance is shared by every cluster in the account. To use larger sizes, create a [billing profile](../cloud-admin/cloud-billing-profile/) for the serverless account. Only Admin users can create a billing profile.

{{<index/block>}}

  {{<index/item
    title="Create and connect"
    body="Create a Serverless Postgres cluster and connect to it."
    href="create-connect/"
    icon="fa-thin fa-bolt">}}

  {{<index/item
    title="Manage a cluster"
    body="Add branches, add compute endpoints, and resize compute."
    href="manage/"
    icon="fa-thin fa-sliders">}}

{{</index/block>}}

## Tasks

Creating, resizing, or deleting a resource records a task. To see a task's progress, or the error if it failed, click **Tasks** in the left navigation and open the task.

## Next steps

- [Explore distributed SQL with Distributed Postgres](../distributed-postgres/)
- [Build an application](/stable/develop/tutorials/build-apps/)

Share your feedback, questions, and suggestions with other users on the [YugabyteDB Community Slack]({{<slack-invite>}}).
