---
title: YugabyteDB Anywhere networking requirements
headerTitle: Hardware requirements for nodes
linkTitle: Hardware requirements
description: Prerequisites for installing YugabyteDB Anywhere.
headContent: Prepare a VM for deployment in a universe
menu:
  preview_yugabyte-platform:
    identifier: server-nodes-hardware
    parent: server-nodes
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#onprem" class="nav-link active" id="onprem-tab" data-toggle="tab"
      role="tab" aria-controls="onprem" aria-selected="true">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="#aws" class="nav-link" id="aws-tab" data-toggle="tab"
      role="tab" aria-controls="aws" aria-selected="false">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#gcp" class="nav-link" id="gcp-tab" data-toggle="tab"
      role="tab" aria-controls="gcp" aria-selected="false">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#azure" class="nav-link" id="azure-tab" data-toggle="tab"
      role="tab" aria-controls="azure" aria-selected="false">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="#k8s" class="nav-link" id="k8s-tab" data-toggle="tab"
      role="tab" aria-controls="k8s" aria-selected="false">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="onprem" class="tab-pane fade show active" role="tabpanel" aria-labelledby="onprem-tab">

Refer to [Hardware requirements](../../../deploy/checklist/#hardware-requirements) for database cluster node hardware requirements. In particular, note the following sections:

- [CPU and RAM](../../../deploy/checklist/#cpu-and-ram)
- [Verify support for SSE2 and SSE4.2](../../../deploy/checklist/#verify-support-for-sse2-and-sse4-2)
- [Disks](../../../deploy/checklist/#disks)

It is recommended to use separate disks for the Linux OS and for the data.

  </div>

  <div id="aws" class="tab-pane fade" role="tabpanel" aria-labelledby="aws-tab">

Refer to [Hardware requirements](../../../deploy/checklist/#hardware-requirements) for database cluster node hardware requirements. In particular, note the following sections:

- [CPU and RAM](../../../deploy/checklist/#cpu-and-ram)
- [Verify support for SSE2 and SSE4.2](../../../deploy/checklist/#verify-support-for-sse2-and-sse4-2)
- [Disks](../../../deploy/checklist/#disks)
- [Amazon Web Services instance type and volume recommendations](../../../deploy/checklist/#amazon-web-services-aws)

  **Note**: The ephemeral disk-based instances (such as i3) in this list come with certain restrictions for Day 2 YugabyteDB Anywhere (YBA) operations. Instances with EBS attached storage are recommended.

  </div>

  <div id="gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">

Refer to [Hardware requirements](../../../deploy/checklist/#hardware-requirements) for database cluster node hardware requirements. In particular, note the following sections:

- [CPU and RAM](../../../deploy/checklist/#cpu-and-ram)
- [Verify support for SSE2 and SSE4.2](../../../deploy/checklist/#verify-support-for-sse2-and-sse4-2)
- [Disks](../../../deploy/checklist/#disks)
- [Google Cloud instance type and volume type recommendations](../../../deploy/checklist/#google-cloud)

  Note that using local disks comes with certain restrictions for Day 2 YBA operations. For YBA, remote disks are recommended.

  </div>

  <div id="azure" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-tab">

Refer to [Hardware requirements](../../../deploy/checklist/#hardware-requirements) for database cluster node hardware requirements. In particular, note the following sections:

- [CPU and RAM](../../../deploy/checklist/#cpu-and-ram)
- [Verify support for SSE2 and SSE4.2](../../../deploy/checklist/#verify-support-for-sse2-and-sse4-2)
- [Disks](../../../deploy/checklist/#disks)
- [Azure instance type and disk type recommendations](../../../deploy/checklist/#azure)

YBA does not support using ephemeral OS disks for Azure DB clusters.

  </div>

  <div id="k8s" class="tab-pane fade" role="tabpanel" aria-labelledby="k8s-tab">

**Compute requirements**

In general, a Kubernetes node that is running YBDB pods is expected to meet the following requirements:

- 5 cores (minimum) or 8 cores (recommended)
- 15 GB RAM (minimum)
- 100 GB SSD disk (minimum)
- 64-bit CPU architecture

However, to estimate the exact resources needed, see [CPU and RAM](../../../deploy/checklist/#cpu-and-ram) for the CPU and RAM requirements for each yb-tserver and yb-master pod. For proper fault tolerance, each Kubernetes node should not run more than one yb-tserver and one yb-master pod. Use these requirements to estimate the total node capacity required in each of the zones that are part of the Kubernetes cluster.

**Storage requirements**

An appropriate storage class has to be specified both during YBA installation and when creating the Kubernetes provider configuration. The type of volume provisioned for YugabyteDB depends on the Kubernetes storage class being used. Consider the following recommendations when selecting or creating a storage class:

- _Use [dynamically provisioned volumes](https://kubernetes.io/docs/concepts/storage/dynamic-provisioning/)_ for YBA and YBDB cluster pods. Set [volume binding mode](https://kubernetes.io/docs/concepts/storage/storage-classes/#volume-binding-mode) on a storage class to `WaitForFirstConsumer` for such volumes. This delays provisioning until a pod using the persistent volume claim (PVC) is created. The pod topology or scheduling constraints are respected.

  Scheduling might fail if the storage volume is not accessible from all the nodes in a cluster and the default volume binding mode is set to `Immediate` for certain regional cloud deployments. The volume may be created in a location or zone that is not accessible to the pod, causing the failure.

  On Google Cloud Provider (GCP), if you choose not to set binding mode to `WaitForFirstConsumer`, you might use regional persistent disks to replicate data between two zones in the same region on Google Kubernetes Engine (GKE). This can be used by the pod when it reschedules to another node in a different zone. For more information, see the following:
  - [Google Kubernetes Engine: persistent volumes and dynamic provisioning](https://cloud.google.com/kubernetes-engine/docs/concepts/persistent-volumes)
  - [Google Cloud: regional persistent disks](https://cloud.google.com/compute/docs/disks/high-availability-regional-persistent-disk)

- _Use a storage class based on remote volumes_ (like cloud provider disks) rather than [local storage volumes attached directly to the kubernetes node](https://kubernetes.io/docs/concepts/storage/volumes/#local) or [local ephemeral volumes](https://kubernetes.io/docs/concepts/storage/ephemeral-volumes/). Local storage provides great performance, but the data is not replicated and can be lost if the node fails or undergoes maintenance, requiring a full remote bootstrap of YugabyteDB data in a pod. Local storage is [not recommended for production use cases](../../../deploy/kubernetes/best-practices/#local-versus-remote-ssds).

- _Use an SSD-based storage class_ and an extent-based file system (XFS), as per recommendations provided in [Deployment checklist - Disks](../../../deploy/checklist/#disks).

- _Set the `allowVolumeExpansion` to `true`_. This enables you to expand the volumes later by performing additional steps if you run out of disk space. Note that some storage providers might not support this setting. For more information, see [Expanding persistent volumes claims](https://kubernetes.io/docs/concepts/storage/persistent-volumes/#expanding-persistent-volumes-claims).

- The storage class for most cloud providers allows further configuration like IOPS and throughput and volume types. It is recommended to configure these parameters to match the recommended parameters as listed in [Public clouds](../../../deploy/checklist/#public-clouds). For information on these configuration parameters, refer to the [AWS](https://github.com/kubernetes-sigs/aws-ebs-csi-driver/blob/master/docs/parameters.md), [GCP](https://cloud.google.com/kubernetes-engine/docs/how-to/persistent-volumes/ssd-pd), and [Azure](https://learn.microsoft.com/en-us/azure/aks/azure-disk-csi#create-a-custom-storage-class) documentation.

The following is a sample storage class YAML file for Google Kubernetes Engine (GKE). You are expected to modify it to suit your Kubernetes cluster:

```yaml
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: yb-storage
provisioner: kubernetes.io/gce-pd
volumeBindingMode: WaitForFirstConsumer
allowVolumeExpansion: true
parameters:
  type: pd-ssd
  fstype: xfs
```

  </div>
</div>
