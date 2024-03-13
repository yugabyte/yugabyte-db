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

## Prerequisites

Ensure your provider configuration is updated with Linux version you want to apply to your universe. Refer to [Create cloud provider configuration](../../configure-yugabyte-platform/set-up-cloud-provider/aws/).

## Upgrade Linux version

To apply operating system patch or upgrade, do the following:

1. Navigate to your universe, click **Actions**, and choose **Upgrade Linux Version**.

1. Select the target version.

1. Specify the delay (in seconds) between restarting servers for the rolling upgrade.
1. Click **Upgrade**.

When finished, you can confirm all nodes in the universe are running correctly in YBA by navigating to the universe **Nodes** tab.
