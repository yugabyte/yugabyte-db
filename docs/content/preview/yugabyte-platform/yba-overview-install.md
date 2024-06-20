---
title: Overview of YugabyteDB Anywhere installation
headerTitle: Installation overview
linkTitle: Installation overview
description: YugabyteDB Anywhere installation overview.
headcontent: What you need to know about deploying YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: yba-overview-install
    parent: yba-overview
    weight: 600
type: docs
---

At a high level, you perform the following tasks to install and use YugabyteDB Anywhere to manage database deployments:

1. Decide on the type of [provider configuration](../yba-overview/#provider-configurations) you will use.

    The provider you choose depends on where you will deploy universes (on your own machines, in the cloud, on Kubernetes), and the permissions you can grant to YugabyteDB Anywhere to perform its tasks.

1. Prepare your infrastructure.

    Set up the necessary VMs, cloud permissions, users, and networking to install YugabyteDB Anywhere and deploy universes. How you prepare your infrastructure depends on the types of providers you want to support.

    See [Prepare your infrastructure](../prepare/).

1. Install the management software (that is, YugabyteDB Anywhere).

    Using YBA Installer, install YugabyteDB Anywhere on a VM.

    See [Install YugabyteDB Anywhere](../install-yugabyte-platform/).

1. Create a Provider Configuration using YugabyteDB Anywhere.

    The provider configuration stores details of the cloud environment where you will deploy your universes.

    See [Create provider configurations](../configure-yugabyte-platform/).

1. Create a database cluster (also referred to as a Universe) using YugabyteDB Anywhere.

    YugabyteDB Anywhere uses the details in the provider configuration to configure and deploy the universe. If using Kubernetes, using Helm charts or the [Kubernetes operator](../../deploy/kubernetes/single-zone/oss/yugabyte-operator/) is also possible.

    See [Create universes](../create-deployments/).

## Prerequisites

To install YugabyteDB Anywhere and be able to deploy YugabyteDB universes requires the following prerequisites:

- [Cloud permissions](../prepare/cloud-permissions/)

  These are required when using Kubernetes or a public cloud provider (AWS, GCP, or Azure). Cloud permissions are not required when using an on-premises provider.

- [Networking connectivity](../prepare/networking/)

  - Between YugabyteDB Anywhere and all of the database cluster nodes
  - Between all the DB nodes
  - Optionally, network connectivity to the Internet for access to resources as required, including the following:
    - Linux OS disk images from cloud service providers
    - Linux software tool yum repositories
    - YugabyteDB database version release binaries
    - Object Storage to be used as backup targets
    - Identity Providers for user authentication

- Server hardware and software

  - [For YugabyteDB Anywhere](../prepare/server-yba/) - 1 or 2 servers (or pods if deployed on K8s) and Linux root permissions
  - [For database clusters](../prepare/server-nodes-hardware/) (also called a universe) - 1, 3, 5, or more servers (or pods if deployed on Kubernetes) and Linux root permissions

See [Prepare your infrastructure](../prepare/) for instructions on meeting these prerequisites.

## Best practices

<!--Fill out the pre-requisites form as you go along. Start installing only after the pre-requisites form is completed. To obtain the form, contact {{% support-platform %}}.-->

If you have development, staging, and production environments (with multiple clusters per environment), deploy one YugabyteDB Anywhere instance (or active/passive high availability pair) for each environment.

## Limitations

YugabyteDB Anywhere can be deployed to on-premises VMs, public cloud VMs, or Kubernetes. The same is true for database clusters. While these are usually de-coupled and independent deployment decisions, if you want to deploy a database cluster to Kubernetes, YugabyteDB Anywhere must also be deployed on Kubernetes.
