---
title: YugabyteDB configuration reference
headerTitle: Configuration
linkTitle: Configuration
description: YugabyteDB configuration reference for core database services, including yb-tserver, yb-master, and yugabyted.
headcontent: Configure core database services
menu:
  v2025.1:
    identifier: configuration
    parent: reference
    weight: 2600
type: indexpage
---

{{<index/block>}}

  {{<index/item
      title="yugabyted reference"
      body="Use the yugabyted utility to launch and manage YugabyteDB."
      href="yugabyted/"
      icon="fa-thin fa-rocket">}}

{{</index/block>}}

### Manual server management

You can use the yb-tserver and yb-master binaries to manually start and configure the servers. For simplified deployment, use [yugabyted](./yugabyted/).

{{<index/block>}}
  {{<index/item
      title="yb-master reference"
      body="Configure YB-Master servers to manage cluster metadata, tablets, and coordination of cluster-wide operations."
      href="yb-master/"
      icon="fa-thin fa-sliders">}}

  {{<index/item
      title="yb-tserver reference"
      body="Configure YB-TServer servers to store and manage data for client applications."
      href="yb-tserver/"
      icon="fa-thin fa-sliders">}}

{{</index/block>}}

### Operating system and ports

{{<index/block>}}
  {{<index/item
      title="Supported operating systems"
      body="Operating systems for deploying YugabyteDB and YugabyteDB Anywhere."
      href="operating-systems/"
      icon="fa-thin fa-window">}}

  {{<index/item
      title="Default ports"
      body="Default ports for APIs, RPCs, and admin web servers."
      href="default-ports/"
      icon="fa-thin fa-network-wired">}}

{{</index/block>}}
