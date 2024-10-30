---
title: YugabyteDB Anywhere networking requirements
headerTitle: Prerequisites to deploy YBA on a VM
linkTitle: Server for YBA
description: Prerequisites for installing YugabyteDB Anywhere.
headContent: Prepare a VM for YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: server-yba-kubernetes
    parent: prepare
    weight: 30
type: docs
rightNav:
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../server-yba/" class="nav-link">
      <i class="fa-solid fa-building"></i>On-premises and public clouds</a>
  </li>

  <li>
    <a href="../server-yba-kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

When installing YBA on an existing Kubernetes cluster, you will install YBA onto a Kubernetes pod using Helm charts.

In addition to the Kubernetes [admin account](../cloud-permissions/cloud-permissions-nodes/) and other Kubernetes permissions, your Kubernetes cluster(s) must meet the following hardware and software requirements.

## Hardware requirements

Each Kubernetes node in the Kubernetes cluster must meet the following requirements:

- 5 cores (minimum) or 8 cores (recommended)
- 15 GB RAM (minimum)
- 100 GB SSD disk (minimum)
- 64-bit CPU architecture

## Software requirements

The Kubernetes cluster and nodes must meet the following software criteria:

- Operating System: any Linux OS
- Kubernetes cluster and Helm version:
  - Kubernetes 1.22+
  - Helm 3.11.3+
  - For OpenShift, versions 4.6+ of the OpenShift Container Platform (OCP) are supported
- Additional software or information
  - A Kubernetes Helm secret (which acts like a license key)

The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes - 1.22 to 1.25
- Helm - 3.11.3

To get the secret, contact your YugabyteDB representative in Sales or Support. It allows download access to the YBA binaries from the YugabyteDB Helm repository.

## Outbound internet access

Verify that your Kubernetes cluster nodes have Internet access and therefore can pull container images from the YugabyteDB `quay.io` container registry.

If the Kubernetes cluster nodes cannot do this, copy the images to your internal registry by following instructions provided in [Pull and push YugabyteDB Docker images to private container registry](../server-nodes-software/software-kubernetes/#pull-and-push-yugabytedb-docker-images-to-private-container-registry).

## High availability deployments

If you plan to deploy YBA in active/passive high-availability mode, you need two independent deployments of YBA in two separate pods; one for the active YBA instance, and another for the passive YBA instance.

If the two pods are on separate Kubernetes clusters, be sure that the pods are exposed through LoadBalancers or Egress so that they can communicate with each other. Separately, you might need to ensure that both YBA HA pods can communicate with all the YugabyteDB database pods. See [Networking requirements for Kubernetes](../networking-kubernetes/) for more details.
