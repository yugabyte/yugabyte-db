---
title: Requirements for servers for database nodes
headerTitle: Prerequisites to deploy YugabyteDB on a VM
linkTitle: Servers for nodes
description: Prerequisites for nodes to be managed by YugabyteDB Anywhere.
headContent: Prepare VMs for database nodes
menu:
  v2.25_yugabyte-platform:
    identifier: server-nodes
    parent: prepare
    weight: 40
type: docs
---

YugabyteDB is designed to run on bare-metal machines, virtual machines (VMs), and cloud provider instances.

The nodes that YugabyteDB Anywhere deploys for use in a YugabyteDB database cluster need to be provisioned for use with YugabyteDB, including the following:

- Compatible Linux OS.
- Other secondary agents, including the node agent, YB Controller backup agent, and Prometheus Node Exporter for host metrics export.
- Additional software and packages, such as Python.

{{<index/block>}}

  {{<index/item
    title="Hardware for nodes"
    body="CPU, disk, and RAM requirements for YugabyteDB."
    href="../server-nodes-hardware/"
    icon="fa-thin fa-microchip">}}

  {{<index/item
    title="Software for nodes"
    body="Linux operating system, and additional packages."
    href="../server-nodes-software/"
    icon="fa-thin fa-binary">}}

{{</index/block>}}
