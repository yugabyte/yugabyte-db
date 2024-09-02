---
title: YugabyteDB configuration reference
headerTitle: Configuration
linkTitle: Configuration
description: YugabyteDB configuration reference for core database services, including yb-tserver, yb-master, and yugabyted.
headcontent: Configure core database services
image: /images/section_icons/deploy/enterprise/administer.png
menu:
  stable:
    identifier: configuration
    parent: reference
    weight: 2600
type: indexpage
---

## Server management

YugabyteDB uses a two-server architecture, with YB-TServers managing the data and YB-Masters managing the metadata. You can use the yb-tserver and yb-master binaries to start and configure the servers.

For simplified deployment, use the yugabyted utility. yugabyted acts as a parent server across the YB-TServer and YB-Masters servers.

{{<index/block>}}
  {{<index/item
      title="yb-tserver reference"
      body="Configure YB-TServer servers to store and manage data for client applications."
      href="yb-tserver/"
      icon="/images/section_icons/reference/configuration/yb-tserver.png">}}

  {{<index/item
      title="yb-master reference"
      body="Configure YB-Master servers to manage cluster metadata, tablets, and coordination of cluster-wide operations."
      href="yb-master/"
      icon="/images/section_icons/reference/configuration/yb-master.png">}}

  {{<index/item
      title="yugabyted reference"
      body="Use the yugabyted utility to launch and manage universes."
      href="yugabyted/"
      icon="/images/section_icons/deploy/manual-deployment.png">}}

{{</index/block>}}

## Operating system, ports, and databases

{{<index/block>}}
  {{<index/item
      title="Supported operating systems"
      body="Operating systems for deploying YugabyteDB and YugabyteDB Anywhere."
      href="operating-systems/"
      icon="/images/section_icons/deploy/enterprise/administer.png">}}

  {{<index/item
      title="Default ports"
      body="Default ports for APIs, RPCs, and admin web servers."
      href="default-ports/"
      icon="/images/section_icons/deploy/enterprise/administer.png">}}

{{</index/block>}}
