---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  preview_yugabyte-platform:
    identifier: cloud-permissions-nodes-1
    parent: cloud-permissions
    weight: 20
type: docs
---

For YugabyteDB Anywhere (YBA) to be able to deploy and manage YugabyteDB clusters, you need to provide YBA with privileges on your cloud infrastructure to create, delete, and modify VMs, mount and unmount disk volumes, and so on.

The more permissions that you can provide, the more YBA can automate.

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

Because you are creating the VMs manually, nodes for on-premises providers don't require any cloud permissions.

With an on-premises provider, permissions against your infrastructure are generally not needed to deploy VMs, modify VMs, and so on.

However, lacking these permissions, although YBA can still operate, YBA can't automate many management tasks and will rely on human manual steps or externally-scripted automation to perform the following tasks:

- create, scale, and delete database clusters with full automation
- apply OS security patches
- recover to full health if a VM fails
