---
title: Apply operating system upgrades and patches to universe nodes
headerTitle: Patch and upgrade the Linux operating system
linkTitle: Patch Linux OS
description: Apply operating system upgrades and patches to universe nodes.
headcontent: Apply operating system upgrades and patches to universe nodes
menu:
  stable_yugabyte-platform:
    identifier: upgrade-nodes-1-csp
    parent: manage-deployments
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../upgrade-nodes-csp/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>
      Public Cloud
    </a>
  </li>

  <li >
    <a href="../upgrade-nodes/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

<!--  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
-->
</ul>

You can apply operating system patches and upgrades to running universes on AWS, GCP, and Azure.

Upgrades are performed via a rolling update, where one node in the universe is taken offline, patched, and restarted before updating the next. The universe continues to function normally during this process, however the upgrade can impact performance. For best results, do the following:

- Perform upgrades during low traffic periods to reduce the impact of rolling updates.
- Avoid performing upgrades during scheduled backups.

## Prerequisites

If your universe uses a Linux version managed by YugabyteDB Anywhere, updated images are automatically added to the provider.

If your universe uses a custom Linux version and you want to upgrade to a new custom version, you must first add the updated custom Linux version to the universe provider configuration. Refer to [Create cloud provider configuration](../../configure-yugabyte-platform/aws/).

Before patching or upgrading the operating system, verify that the Linux version image that you intend to use can successfully run on a standalone virtual machine (VM) with the same instance type as the nodes you are patching. You can do this by creating a new VM with the same instance type as your universe and using the selected Linux version for the boot disk and verifying that the VM boots successfully.

## Upgrade Linux version

To apply operating system patch or upgrade, do the following:

1. Navigate to your universe, click **Actions**, and choose **Upgrade Linux Version**.

1. Select the target version.

1. If the universe has a read replica, to select a different Linux version for the replica, deselect the **Use the same version for the primary cluster and the read replicas** option and select the version to use for the read replica.

1. Specify the delay (in seconds) between restarting servers for the rolling upgrade.
1. Click **Upgrade**.

When finished, you can confirm all nodes in the universe are running correctly in YBA by navigating to the universe **Nodes** tab.
