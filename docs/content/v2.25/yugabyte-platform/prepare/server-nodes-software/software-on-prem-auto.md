---
title: Legacy automatic on-premises node provisioning
headerTitle: Legacy provisioning
linkTitle: Legacy provisioning
description: How to provision on-premises provider nodes using legacy automatic method.
headContent: Prepare a VM for deploying universes on-premises
menu:
  v2.25_yugabyte-platform:
    identifier: software-on-prem-2-auto
    parent: software-on-prem
    weight: 10
type: docs
---

{{< warning title="Legacy provisioning deprecated" >}}
Legacy provisioning of on-premises nodes is deprecated. Before you can upgrade YugabyteDB Anywhere to v2025.2, all universes must be updated to use node agent and provisioned using the [node agent script](../software-on-prem/#run-the-provisioning-script). For more information, refer to [Prepare to upgrade](../../../upgrade/prepare-to-upgrade/).
{{< /warning >}}

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../software-on-prem-legacy/" class="nav-link">
      How to Choose
    </a>
  </li>

  <li>
    <a href="../software-on-prem-auto/" class="nav-link active">
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

When YugabyteDB Anywhere (YBA) has access to an SSH user with passwordless sudo privileges (for example, the `ec2-user` on AWS EC2 instances), then YBA can provision the VMs automatically.

## With Internet or Yum connectivity

If your VM has Internet or Yum connectivity, you must provide to YBA a VM with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled, root-privileged user. YBA uses this user to automatically perform additional Linux configuration, such as creating the `yugabyte` user, updating the file descriptor settings via ulimits, and so on.
- [Additional software](../#additional-software)

Take the time now to prepare the VM.

- Save the SSH-enabled, root-privileged user credentials (username and SSH Private Key Content PEM file).
- Save the VM IP addresses for later when creating the on-premises provider.

| Save for later | To configure |
| :--- | :--- |
| SSH-enabled, root-privileged user name | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| SSH-enabled, root-privileged Private Key Content PEM file | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| VM IP addresses | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |

## No Internet nor Yum connectivity

If your VM doesn't have Internet or Yum connectivity, you must provide to YBA a VM with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled, root-privileged user. YBA uses this user to automatically perform additional Linux configuration, such as creating the `yugabyte` user, updating the file descriptor settings via ulimits, and so on.
- [Additional software](../#additional-software)
- [Additional software for airgapped](../#additional-software-for-airgapped-deployment)

Take the time now to prepare the VM.

- Save the SSH-enabled, root-privileged user credentials (username and SSH Private Key Content PEM file).
- Save the VM IP or DNS addresses for later when creating the on-premises provider.

| Save for later | To configure |
| :--- | :--- |
| SSH-enabled, root-privileged user name | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| SSH-enabled, root-privileged Private Key Content PEM file | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| VM IP addresses | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
