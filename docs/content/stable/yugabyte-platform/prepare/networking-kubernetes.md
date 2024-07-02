---
title: YugabyteDB Anywhere networking requirements for Kubernetes
headerTitle: Networking requirements for Kubernetes
linkTitle: Kubernetes
description: Added requirements for deploying YugabyteDB Anywhere on Kubernetes.
headContent: Additional requirements for YugabyteDB Anywhere on Kubernetes
menu:
  stable_yugabyte-platform:
    identifier: networking-kubernetes
    parent: networking
    weight: 10
type: docs
---

## Basic requirements

YugabyteDB Anywhere (YBA) and Kubernetes pods require network connectivity as described in [Networking](../networking/).

If you use [Network Policy](https://kubernetes.io/docs/concepts/services-networking/network-policies/) resources to restrict such traffic, you will need to create policies to allow the specific network connectivity required.

## Multi cluster Kubernetes environment

When it comes to networking, Kubernetes clusters are typically restricted to reside in a single region; they cannot span physical regions.

This poses a challenge for the following deployments:

1. Multi-region YugabyteDB universes.
1. xCluster replication, where two universes are deployed in different regions.
1. You want YBA (residing in one region) to manage one or more YugabyteDB universes in other regions.
1. You want to deploy YBA in an active-passive high availability (HA) pair, with one YBA in one region and the standby YBA in a different region. In this case, you need to ensure that both YBAs can reach all DB pods in all regions.

For all of these cases, you must use one Kubernetes cluster per region and use [MCS](https://multicluster.sigs.k8s.io/concepts/multicluster-services-api/) or [Istio](https://istio.io/) to enable the required connectivity. You will need to ensure that pods in each Kubernetes cluster (YBA or YBDB) can communicate with pods in a different Kubernetes cluster, using one of the following approaches.

In the general case, the following are the prerequisites for YugabyteDB Anywhere to work across multiple Kubernetes clusters:

- Pod IP address connectivity must be present between the Kubernetes clusters.
- Each pod and service must have globally unique IP address. One way to achieve this is to set up your K8s clusters with non-overlapping IP address ranges.
- There must be DNS connectivity between the clusters. ClusterIP and headless service FQDNs (including the individual pod FQDNs) exposed in one Kubernetes cluster must be resolvable in all the other Kubernetes clusters.
- YugabyteDB Anywhere must have network connectivity to the control plane of all the Kubernetes clusters.

To achieve network connectivity across multiple Kubernetes clusters, you can set up your networking to directly meet the requirements, or alternatively set up Multi-Cluster Services API (MCS) and/or Istio and configure YBA support to meet the same requirements. Note that MCS support in YugabyteDB Anywhere is currently in Early Access {{<badge/ea>}}.

### Prepare Kubernetes clusters for GKE MCS

GKE MCS allows clusters to be combined as a fleet on Google Cloud. These fleet clusters can export services, which enables you to do cross-cluster communication. For more information, see [Multi-cluster Services](https://cloud.google.com/kubernetes-engine/docs/concepts/multi-cluster-services) in the Google Cloud documentation.

To enable MCS on your GKE clusters, see [Configuring multi-cluster Services](https://cloud.google.com/kubernetes-engine/docs/how-to/multi-cluster-services). Note down the unique membership name of each cluster in the fleet, it will be used during the cloud provider setup in YBA.

### Prepare OpenShift clusters for MCS

Red Hat OpenShift Container Platform uses the Advanced Cluster Management for Kubernetes (RHACM) and its Submariner add-on to enable MCS. At a very high level this involves following steps:

Create a management cluster and install RHACM on it. For details, see [Installing Red Hat Advanced Cluster Management for Kubernetes](https://access.redhat.com/documentation/en-us/red_hat_advanced_cluster_management_for_kubernetes/2.1/html/install/installing) in the Red Hat documentation.

Provision the OpenShift clusters which will be connected together. Ensure that the CIDRs mentioned in the cluster configuration file at networking.clusterNetwork, networking.serviceNetwork, and networking.machineNetwork are non-overlapping across the multiple clusters. You can find more details about these options in provider-specific sections under the OpenShift Container Platform installation overview (look for sections named "Installing a cluster on [provider name] with customizations").

Import the clusters into RHACM as a cluster set, and install the Submariner add-on on them. For more information, see [Configuring Submariner](https://access.redhat.com/documentation/en-us/red_hat_advanced_cluster_management_for_kubernetes/2.7/html/add-ons/add-ons-overview#configuring-submariner).

Note down the cluster names from the cluster set, as these will be used during the cloud provider setup in YBA.

### Prepare Istio service mesh-based clusters for MCS

An Istio service mesh can span multiple clusters, which allows you to configure MCS. It supports different topologies and network configurations. To install an Istio mesh across multiple Kubernetes clusters, see [Install Multicluster](https://istio.io/latest/docs/setup/install/multicluster/) in the Istio documentation.

The Istio configuration for each cluster should have following options:

```yaml
apiVersion: install.istio.io/v1alpha1
kind: IstioOperator
spec:
  meshConfig:
    defaultConfig:
      proxyMetadata:
        ISTIO_META_DNS_CAPTURE: "true"
        ISTIO_META_DNS_AUTO_ALLOCATE: "true"
  # rest of the configuration…
```

Refer to [Multi-Region YugabyteDB Deployments on Kubernetes with Istio](https://www.yugabyte.com/blog/multi-region-yugabytedb-deployments-on-kubernetes-with-istio/) for a step-by-step guide and an explanation of the options being used.
