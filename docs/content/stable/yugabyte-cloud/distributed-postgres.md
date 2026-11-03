---
title: Distributed Postgres in YugabyteDB Aeon
headerTitle: Distributed Postgres
linkTitle: Distributed Postgres
description: Distributed Postgres clusters in YugabyteDB Aeon for production workloads.
headContent: Distributed PostgreSQL, fully managed by Yugabyte
menu:
  stable_yugabyte-cloud:
    identifier: distributed-postgres
    parent: yugabytedb-managed
    weight: 25
    params:
      classes: separator
type: docs
showRightNav: true
---

Distributed Postgres is a fully managed YugabyteDB service in YugabyteDB Aeon. Clusters run on multiple nodes, and can span availability zones and regions on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP). Use it for production workloads that need high availability, horizontal scale, and geo-distribution.

A Distributed Postgres cluster includes the following:

- Fault tolerance across nodes, availability zones, or regions.
- Single-region and multi-region topologies, including geo-partitioning and read replicas.
- Horizontal and vertical scaling, including autoscaling.
- Private networking with VPC peering or a private link.
- Backups, point-in-time recovery, and maintenance windows.

Clusters are billed per vCPU. [Request a free trial](../managed-freetrial/) to create clusters with trial credits.

{{<index/block>}}

  {{<index/item
    title="Deploy clusters"
    body="Plan a topology and create single-region or multi-region clusters."
    href="../cloud-basics/"
    icon="fa-thin fa-city">}}

  {{<index/item
    title="Secure clusters"
    body="Authorize network access, manage database users, and download certificates."
    href="../cloud-secure-clusters/"
    icon="fa-thin fa-lock-keyhole">}}

  {{<index/item
    title="Connect to clusters"
    body="Connect from the browser, a client shell, or an application."
    href="../cloud-connect/"
    icon="fa-thin fa-terminal">}}

  {{<index/item
    title="Manage clusters"
    body="Scale clusters, configure backups and maintenance, and pause or delete clusters."
    href="../cloud-clusters/"
    icon="fa-thin fa-sliders">}}

  {{<index/item
    title="Alerts and monitoring"
    body="Set alerts and monitor cluster performance and activity."
    href="../cloud-monitor/"
    icon="fa-thin fa-chart-column">}}

{{</index/block>}}
