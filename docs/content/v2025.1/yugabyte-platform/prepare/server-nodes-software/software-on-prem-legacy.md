---
title: YugabyteDB Anywhere legacy on-premises node provisioning
headerTitle: Legacy provisioning
linkTitle: Legacy provisioning
description: Software requirements for on-premises provider legacy provisioning.
headContent: Prepare a VM for deploying universes on-premises
menu:
  v2025.1_yugabyte-platform:
    identifier: software-on-prem-1-intro
    parent: software-on-prem
    weight: 10
type: docs
---

{{< warning title="Legacy provisioning deprecated" >}}
Legacy provisioning of on-premises nodes is deprecated. Before you can upgrade YugabyteDB Anywhere to v2025.2, all universes must be updated to use node agent and provisioned using the [node agent script](../software-on-prem/#run-the-provisioning-script). For more information, refer to [Prepare to upgrade](../../../upgrade/prepare-to-upgrade/).
{{< /warning >}}

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../software-on-prem-legacy/" class="nav-link active">
      How to Choose
    </a>
  </li>

  <li>
    <a href="../software-on-prem-auto/" class="nav-link">
      <i class="fa-regular fa-wand-magic-sparkles"></i>
      Automatic
    </a>
  </li>

  <li>
    <a href="../software-on-prem-assist/" class="nav-link">
      <i class="fa-regular fa-scroll"></i>
      Assisted
    </a>
  </li>

  <li>
    <a href="../software-on-prem-manual/" class="nav-link">
      <i class="icon-shell" aria-hidden="true"></i>
      Fully manual
    </a>
  </li>
</ul>

How you provision nodes for use with an on-premises provider depends on the SSH access that you can grant YugabyteDB Anywhere to provision nodes.

| SSH mode | Description | Notes | For more details |
| :--- | :--- | :--- | :--- |
| Permissive | You can allow SSH to a root-privileged user, AND<br>You can provide YugabyteDB Anywhere with SSH login credentials for that user. | For example, the ec2-user for AWS EC2 instances meets this requirement. In this mode, YugabyteDB Anywhere will sign in to the VM and automatically provision the nodes. | See [Automatic Provisioning](../software-on-prem-auto/). |
| Medium | You can allow SSH to a root-privileged user, AND<br>You can't provide YugabyteDB Anywhere with SSH login credentials for that user; however you can enter the password interactively. | In this mode, you run a script on the VM, and are prompted for a password for each sudo action to install the required software. | See [Assisted Manual Provisioning](../software-on-prem-assist/). |
| Restrictive | All other cases (you disallow SSH login to a root-privileged user at setup time). | In this mode, you'll manually install each prerequisite software component. | See [Fully Manual Provisioning](../software-on-prem-manual/). |

Note that, for Permissive and Medium modes, the root-privileged SSH user can have any username except `yugabyte`. When YugabyteDB Anywhere later uses the provided SSH user to sign in and prepare the node, it will automatically create and configure a second user named `yugabyte`, which will be largely de-privileged, and will have very limited sudo commands available to it. YugabyteDB Anywhere uses the `yugabyte` user to run the various YugabyteDB software processes.

SSH access is required for initial setup of a new database cluster (and when adding new nodes to the cluster). After setup is completed, SSH can be disabled, at your option. Leaving SSH enabled, however, is still recommended to provide access to nodes for troubleshooting.

## Best practices

When creating your VMs, create at least two virtual disks: one as the boot disk, and another for data and logs.
