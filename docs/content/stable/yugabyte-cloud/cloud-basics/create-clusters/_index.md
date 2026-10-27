---
title: Create distributed clusters
headerTitle: Create your cluster
linkTitle: Create your cluster
description: Create distributed clusters in YugabyteDB Aeon.
headcontent: Create a distributed cluster in YugabyteDB Aeon
menu:
  stable_yugabyte-cloud:
    identifier: create-clusters
    parent: cloud-basics
    weight: 25
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Serverless Postgres"
    body="Create an AMP Managed cluster for agents and tenants."
    href="/stable/yugabyte-cloud/serverless/"
    icon="fa-thin fa-bolt">}}

  {{<index/item
    title="Single region"
    body="Deploy single-region clusters with node- and availability zone-level fault tolerance."
    href="create-single-region/"
    icon="fa-thin fa-city">}}

  {{<index/item
    title="Replicate across regions"
    body="Create a stretched multi-region synchronous cluster."
    href="create-clusters-multisync/"
    icon="fa-thin fa-planet-moon">}}

  {{<index/item
    title="Partition by region"
    body="Pin data to specific geographical regions."
    href="create-clusters-geopartition/"
    icon="fa-thin fa-planet-moon">}}

{{</index/block>}}

## Sandbox clusters

You can't create new Sandbox clusters; use a free [Serverless Postgres cluster](../../serverless/) instead, or [Request a free trial](../../managed-freetrial/) to try out larger distributed clusters with more resources.

Existing Sandbox clusters continue to work. 

Sandbox clusters are single-node, free clusters with the following limits:

- One Sandbox cluster per account.
- Sandbox clusters can't be scaled, and have no backups.
- Network access is limited using IP allow lists. VPC networking is not supported.
- Up to 15 simultaneous connections, and a maximum of 500 tables or 12.5 million rows. More than that may result in increased latencies, dropped connections, or out-of-memory errors.

Sandbox clusters are paused after 10 days of inactivity, and you receive an email notification. To resume a paused cluster, sign in to YugabyteDB Aeon, select the cluster on the **Clusters** page, and click **Resume**. A second notification is sent after 13 days of inactivity. Paused clusters are deleted after 15 days of inactivity. Idle cluster deletion jobs run daily, so your cluster may be paused or deleted up to 24 hours after the time in the notification email.

Once a Sandbox cluster is deleted, you cannot create a new one.

To keep a cluster from being paused, perform any of the following:

- Any SELECT, UPDATE, INSERT, or DELETE database operation.
- Create or delete tables.
- Add or remove IP allow lists.
