---
title: Prepare your infrastructure
headerTitle: Prepare your infrastructure
linkTitle: Prepare
description: Prepare cloud permissions, networking, and servers for YugabyteDB Anywhere.
headcontent: Prepare cloud permissions, networking, and servers for YugabyteDB Anywhere
image: fa-thin fa-clipboard-list
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: prepare
weight: 605
type: indexpage
---

YugabyteDB Anywhere is a control plane used to deploy and manage YugabyteDB database clusters.

To prepare your infrastructure for YugabyteDB Anywhere, you need to do the following:

- [Configure cloud permissions](./cloud-permissions/)

  If you are using a public cloud provider (AWS, GCP, or Azure) or Kubernetes, you need to configure your cloud environment with the appropriate users and security permissions required by YBA.

  If you are using an on-premises provider, no cloud permissions are required. However, if you back up to a cloud object store, use a cloud KMS, or export logs, metrics, or data to a cloud service, you may need to set up some permissions.

- [Configure networking](./networking/)

  YugabyteDB Anywhere requires access to specific ports on the server where it is installed, as well as access to the servers or VMs that it deploys for use in database clusters.

- [Prepare a server for YugabyteDB Anywhere](./server-yba/)

  You must prepare a server where you will install YugabyteDB Anywhere. This server must meet the following prerequisites:

  - Minimum hardware requirements for architecture, CPU, and disk
  - Minimum software requirements, including Linux OS and additional software and utilities

- Prepare servers for database nodes

  The nodes that YugabyteDB Anywhere deploys for use in a YugabyteDB database cluster need to be provisioned for use with YugabyteDB. This includes the following:

  - [Minimum hardware requirements](./server-nodes-hardware/) for architecture, CPU, and disk
  - [Minimum software requirements](./server-nodes-software/), including Linux OS and additional software and utilities
