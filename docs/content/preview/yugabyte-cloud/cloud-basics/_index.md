---
title: Deploy clusters in YugabyteDB Aeon
linkTitle: Deploy clusters
description: Deploy clusters in YugabyteDB Aeon.
headcontent: Deploy production-ready clusters
image: /images/section_icons/index/quick_start.png
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-basics
    weight: 25
type: indexpage
---

Deploy single- and multi-region production clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

### Prepare

{{<index/block>}}

  {{<index/item
    title="Plan your cluster"
    body="Plan your production deployment."
    href="create-clusters-overview/"
    icon="/images/section_icons/introduction/benefits.png">}}

  {{<index/item
    title="Choose a topology"
    body="Choose the topology best suited to your requirements for latency, availability, and geo-distribution."
    href="create-clusters-topology/"
    icon="/images/section_icons/explore/planet_scale.png">}}

  {{<index/item
    title="VPC networks"
    body="Connect clusters privately to other resources on the same cloud provider."
    href="cloud-vpcs/"
    icon="/images/section_icons/manage/backup.png">}}

{{</index/block>}}

### Deploy

#### Single region

{{<index/block>}}

  {{<index/item
    title="Single region multi zone"
    body="Deploy single-region clusters with node- and availability zone-level fault tolerance."
    href="create-clusters/create-single-region/"
    icon="/images/section_icons/quick_start/create_cluster.png">}}

{{</index/block>}}

#### Multi region

{{<index/block>}}

  {{<index/item
    title="Replicate across regions"
    body="Create a stretched multi-region synchronous cluster."
    href="create-clusters/create-clusters-multisync/"
    icon="/images/section_icons/explore/planet_scale.png">}}

  {{<index/item
    title="Partition by region"
    body="Use geo-partitioning to pin data to specific geographical regions."
    href="create-clusters/create-clusters-geopartition/"
    icon="/images/section_icons/explore/planet_scale.png">}}

{{</index/block>}}
