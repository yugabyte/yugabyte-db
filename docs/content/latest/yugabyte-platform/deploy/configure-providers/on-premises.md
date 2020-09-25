---
title: Configure the on-premises data centers
headerTitle: Configure the on-premise providers
linkTitle: 4. Configure the providers
description: Configure the on-premises data centers.
menu:
  latest:
    identifier: configure-providers-1-on-premises
    parent: deploy-yugabyte-platform
    weight: 627
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/on-premises" class="nav-link active">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/deploy/configure-providers/vmware-tanzu" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

</ul>

## Step 1. Configure the on-premises provider using the docs instructions

1. Specify the SSH user as the `yw` user required for provisioning YugabyteDB nodes.
2. Ensure that the SSH key is pasted correctly (Supported format is `RSA`)
3. Keep the default home directory path (`/home/yugabyte`).
4. For mount paths, use a mount point with enough space to contain your node density. Use `/data`.  If you have multiple drives, add these as a comma-separated list: `/mnt/d0,/mnt/d1`.

## Step 2. Provision the YugabyteDB nodes

Follow the steps below to provision as many nodes as your application requires:

1. Add the YugabyteDB node IP addresses to the on-premises cloud provider using the **Manage Instances** workflow.
2. Use DNS names or IP addresses when adding instances.
3. Use [Create a multi-zone universe](../manage/create-universe-multi-zone/).

---

???

![Configure On-Premises Data center Provider](/images/ee/onprem/configure-onprem-1.png)

![On-Premises Data center Provider Configuration in Progress](/images/ee/onprem/configure-install-onprem-2.png)

![On-Premises Data center Provider Configured Successfully](/images/ee/onprem/configure-onprem-3.png)
http://localhost:1313/images/ee/onprem/configure-install-onprem-2.png
https://docs.yugabyte.com/images/ee/onprem/configure-onprem-1.png