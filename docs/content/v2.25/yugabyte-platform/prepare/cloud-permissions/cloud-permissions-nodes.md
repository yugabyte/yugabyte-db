---
title: Cloud setup for deploying universe nodes
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  v2.25_yugabyte-platform:
    identifier: cloud-permissions-nodes-1
    parent: cloud-permissions
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-permissions-nodes/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-gcp" class="nav-link">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-azure" class="nav-link">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-k8s" class="nav-link">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

## On-premises

Because you are creating the VMs manually (on a private cloud, bare metal, or cloud provider), nodes for on-premises providers don't require any cloud permissions.

If you will be deploying on-premises universes in AWS, you can attach a service account or IAM role to nodes to be used to access storage in S3. The service account or IAM role used should be sufficient to access S3. For more information, refer to [Permissions to back up and restore](../cloud-permissions-storage/).

With an on-premises provider, permissions against your infrastructure are generally not needed to deploy VMs, modify VMs, and so on.

Provisioning VMs requires root access, but after VMs have been provisioned with the operating system, required software, and node agent, root and sudo access is no longer required.

For more information, refer to [Automatically provision on-premises nodes](../../server-nodes-software/software-on-prem/).
