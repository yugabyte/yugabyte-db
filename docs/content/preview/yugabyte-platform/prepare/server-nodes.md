---
title: Requirements for servers for universe nodes
headerTitle: Prerequisites to deploy YugabyteDB on a VM
linkTitle: Servers for nodes
description: Prerequisites for nodes to be managed by YugabyteDB Anywhere.
headContent: Prepare VMs for database nodes
menu:
  preview_yugabyte-platform:
    identifier: server-nodes
    parent: prepare
    weight: 40
type: docs
---

The nodes that YugabyteDB Anywhere deploys for use in a YugabyteDB database cluster need to be provisioned for use with YugabyteDB. This includes the following:

- [Minimum hardware requirements for architecture, CPU, and disk](../server-nodes-hardware/).

    YugabyteDB is designed to run on bare-metal machines, virtual machines (VMs), and cloud provider instances.

- [Minimum software requirements](../server-nodes-software/), including Linux OS and additional software and utilities.

    Nodes that are deployed for use in a YugabyteDB cluster must be provisioned with a compatible Linux OS; YugabyteDB (including the YB-Master and YB-TServer services of the cluster); other secondary agents, including the node agent, YB-Controller backup agent, and Prometheus Node Exporter for host metrics export; and additional software and packages, such as Python.
