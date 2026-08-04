---
title: Deploy clusters in YugabyteDB Aeon
headerTitle: Deploy clusters
linkTitle: Deploy clusters
description: Deploy clusters in YugabyteDB Aeon.
headcontent: Deploy production-ready clusters
menu:
  stable_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-basics
    weight: 25
type: indexpage
---

{{< page-finder/head text="Deploy YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../deploy/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/create-deployments/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" current="" >}}
{{< /page-finder/head >}}

Deploy single- and multi-region production clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

### Prepare

{{<index/block>}}

  {{<index/item
    title="Plan your cluster"
    body="Plan your production deployment."
    href="create-clusters-overview/"
    icon="fa-thin fa-clipboard">}}

  {{<index/item
    title="Choose a topology"
    body="Choose the topology best suited to your requirements for latency, availability, and geo-distribution."
    href="create-clusters-topology/"
    icon="fa-thin fa-chart-network">}}

  {{<index/item
    title="VPC networks"
    body="Connect clusters privately to other resources on the same cloud provider."
    href="cloud-vpcs/"
    icon="fa-thin fa-cloud">}}

{{</index/block>}}

### Deploy

#### Single region

{{<index/block>}}

  {{<index/item
    title="Single region multi zone"
    body="Deploy single-region clusters with node- and availability zone-level fault tolerance."
    href="create-clusters/create-single-region/"
    icon="fa-thin fa-city">}}

{{</index/block>}}

#### Multi region

{{<index/block>}}

  {{<index/item
    title="Replicate across regions"
    body="Create a stretched multi-region synchronous cluster."
    href="create-clusters/create-clusters-multisync/"
    icon="fa-thin fa-planet-moon">}}

  {{<index/item
    title="Partition by region"
    body="Use geo-partitioning to pin data to specific geographical regions."
    href="create-clusters/create-clusters-geopartition/"
    icon="fa-thin fa-planet-moon">}}

{{</index/block>}}
