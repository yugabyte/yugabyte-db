---
title: Prerequisites - Kubernetes
headerTitle: Prerequisites for YBA
linkTitle: YBA prerequisites
description: Prerequisites for installing YugabyteDB Anywhere in your Kubernetes environment
headContent: What you need to install YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: prerequisites-kubernetes
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

You can install YugabyteDB Anywhere (YBA) using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| YBA Installer | yba-ctl CLI | You are performing a new installation. |
| Replicated | Docker containers | Your installation already uses Replicated. |
| Kubernetes | Helm chart | You're deploying in Kubernetes. |

All installation methods support installing YBA with and without (airgapped) Internet connectivity.

Licensing (such as a license file in the case of Replicated, or appropriate repository access in the case of Kubernetes) may be required prior to installation.  Contact {{% support-platform %}} for assistance.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../installer/" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>YBA Installer</a>
  </li>
  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

## Hardware requirements

A Kubernetes node is expected to meet the following requirements:

- 5 cores (minimum) or 8 cores (recommended)
- 15 GB RAM (minimum)
- 250 GB SSD disk (minimum)
- 64-bit CPU architecture

## Prepare the cluster

The minimum version for a Kubernetes cluster and Helm chart are as follows:

- Kubernetes 1.22
- Helm 3.11.3

The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes - 1.22 to 1.25
- Helm - 3.11.3

Before installing YugabyteDB Anywhere, verify that you have the following:

- A Kubernetes cluster with nodes configured according to the [hardware requirements](#hardware-requirements).
- A client environment with the kubectl and Helm command-line tools configured with a service account or user that has admin access to a  single namespace on the subject Kubernetes cluster.
- A Kubernetes secret obtained from {{% support-platform %}}.

In addition, ensure the following:

- The nodes can pull container images from the [quay.io](https://quay.io/) container registry. If the nodes cannot do this, you need to prepare these images in your internal registry by following instructions provided in [Pull and push YugabyteDB Docker images to private container registry](../../prepare-environment/kubernetes#pull-and-push-yugabytedb-docker-images-to-private-container-registry).
- Core dumps are enabled and configured on the underlying Kubernetes node. For details, see [Specify ulimit and remember the location of core dumps](../../prepare-environment/kubernetes#specify-ulimit-and-remember-the-location-of-core-dumps).
- You have the kube-state-metrics add-on version 1.9 in your Kubernetes cluster. For more information, see [Install kube-state-metrics](../../prepare-environment/kubernetes#install-kube-state-metrics).
- A load balancer controller is available in your Kubernetes cluster.
- A StorageClass is available with the SSD and `WaitForFirstConsumer` preferably set to `allowVolumeExpansion`. For more information, see [Configure storage class](../../prepare-environment/kubernetes/#configure-storage-class).

## Multi cluster Kubernetes environment

When you want to create a multi-region YugabyteDB universe or two universes from different regions replicating using xCluster, you need to use one Kubernetes cluster per region. In the common case, following are the prerequisites for YugabyteDB Anywhere to work across multiple Kubernetes clusters:

- Pod IP address connectivity should be present between the clusters. Each pod and service should have a unique IP address across the clusters (non-overlapping addresses).
- There should be DNS connectivity between the clusters. ClusterIP and headless service FQDNs (including the individual pod FQDNs) exposed in one Kubernetes cluster should be resolvable in all the other Kubernetes clusters.
- YugabyteDB Anywhere should have access to the control plane of all the Kubernetes clusters, typically via a `kubeconfig` file of a service account. It should be installed on one of the connected clusters.

Alternatively, you can set up [Multi-Cluster Services API](https://git.k8s.io/enhancements/keps/sig-multicluster/1645-multi-cluster-services-api) (MCS). For more details on the setup, see [Configure Kubernetes multi-cluster environment](../../../configure-yugabyte-platform/set-up-cloud-provider/kubernetes#configure-kubernetes-multi-cluster-environment). Note that MCS support in YugabyteDB Anywhere is currently in [Beta](/preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag).
