---
title: Create YugabyteDB universe deployments
headerTitle: Create universes
linkTitle: Create universes
description: Create YugabyteDB universe deployments.
headcontent: Deploy to the public cloud, a private data center, or Kubernetes
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: create-deployments
    weight: 630
type: indexpage
---

{{< page-finder/head text="Deploy YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../deploy/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" current="" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-basics/" >}}
{{< /page-finder/head >}}

YugabyteDB Anywhere can create a YugabyteDB universe with many instances (virtual machines, pods, and so on, provided by IaaS), logically grouped together to form one distributed database.

A universe includes one primary cluster and, optionally, one read replica cluster. All instances belonging to a cluster run on the same type of cloud provider instance.

For information on modifying or scaling an existing universe, refer to [Scale and edit universes](../scale-deployments/).

{{<index/block>}}

  {{<index/item
    title="Plan your universe"
    body="Review topology, placement, hardware, and security before you deploy."
    href="create-universes-overview/"
    icon="fa-thin fa-map">}}

  {{<index/item
    title="Create a universe"
    body="Deploy a universe across multiple zones or regions."
    href="create-universes-wizard/"
    icon="fa-thin fa-city">}}

  {{<index/item
    title="Add read replica"
    body="Add a read replica cluster to a universe."
    href="read-replicas/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Place YB-Masters on dedicated nodes"
    body="Create a universe with YB-Master and YB-TServer processes on dedicated nodes."
    href="dedicated-master/"
    icon="fa-thin fa-eye-evil">}}

  {{<index/item
    title="xCluster Replication"
    body="Replicate data between independent YugabyteDB universes."
    href="xcluster-replication/"
    icon="fa-thin fa-arrows-left-right">}}

  {{<index/item
    title="Create a multi-cloud universe"
    body="Deploy a multi-cloud universe."
    href="create-universe-multi-cloud/"
    icon="fa-thin fa-clouds">}}

  {{<index/item
    title="Connect to a universe"
    body="Connect to your universe using a client shell."
    href="connect-to-universe/"
    icon="fa-thin fa-wifi">}}

{{</index/block>}}
