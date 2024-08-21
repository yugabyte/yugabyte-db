---
title: Create YugabyteDB universe deployments
headerTitle: Create universes
linkTitle: Create universes
description: Create YugabyteDB universe deployments.
image: fa-thin fa-cloud-plus
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: create-deployments
    weight: 630
type: indexpage
---

YugabyteDB Anywhere can create a YugabyteDB universe with many instances (virtual machines, pods, and so on, provided by IaaS), logically grouped together to form one distributed database.

A universe includes one primary cluster and, optionally, one read replica cluster. All instances belonging to a cluster run on the same type of cloud provider instance.

{{<index/block>}}

  {{<index/item
    title="Create a multi-zone universe"
    body="Deploy a multi-zone universe."
    href="create-universe-multi-zone/"
    icon="fa-thin fa-city">}}

  {{<index/item
    title="Create a multi-region universe"
    body="Deploy a multi-region universe."
    href="create-universe-multi-region/"
    icon="fa-thin fa-planet-moon">}}

  {{<index/item
    title="Create a multi-cloud universe"
    body="Deploy a multi-cloud universe."
    href="create-universe-multi-cloud/"
    icon="fa-thin fa-clouds">}}

  {{<index/item
    title="Create a read-replica cluster"
    body="Create a read-replica cluster for a universe."
    href="read-replicas/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Place YB-Masters on dedicated nodes"
    body="Create a universe with YB-Master and YB-TServer processes on dedicated nodes."
    href="dedicated-master/"
    icon="fa-thin fa-eye-evil">}}

  {{<index/item
    title="Connect to a universe"
    body="Connect to your universe using a client shell."
    href="connect-to-universe/"
    icon="fa-thin fa-wifi">}}

{{</index/block>}}
