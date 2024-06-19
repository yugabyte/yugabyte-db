---
title: YugabyteDB Anywhere on-premises node provisioning
headerTitle: Provisioning on-premises nodes
linkTitle: Provision nodes
description: Software requirements for on-premises provider nodes.
headContent: How to meet the software prerequisites with assisted manual provisioning
menu:
  preview_yugabyte-platform:
    identifier: software-on-prem-2-assist
    parent: software-on-prem
    weight: 10
type: docs
---

{{<tip title="For v2.20 and earlier">}}
For instructions on preparing nodes for on-premises configurations in v2.20 and earlier, see [Create on-premises provider configuration](/v2.20/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/).
{{</tip>}}

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../software-on-prem-auto/" class="nav-link">
      <i class="fa-regular fa-wand-magic-sparkles"></i>
      Automatic
    </a>
  </li>

  <li>
    <a href="../software-on-prem-assist/" class="nav-link active">
      <i class="fa-regular fa-scroll"></i>
      Assisted manual
    </a>
  </li>

  <li>
    <a href="../software-on-prem-manual/" class="nav-link">
      <i class="icon-shell" aria-hidden="true"></i>
      Fully manual
    </a>
  </li>
</ul>

Use Assisted Manual Provisioning in the following case:

- You can allow SSH to a root-priveleged user, AND
- You can't provide YBA with SSH login credentials for that user; however you can enter the password manually interactively.

SSH is required only during initial provisioning of the nodes. After a node is provisioned, you can disable SSH.

In this provisioning workflow, after creating the VMs, installing YBA, and creating an on-premises provider, YBA creates a script (`provision_instance.py`) for you to use to provision the nodes interactively. The script signs in to each of your VMs with SSH credentials that you provide (including username and password), and prepares the VM.

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
- Save the VM IP addresses for later when creating the on-premises provider.

| Save for later | To configure |
| :--- | :--- |
| SSH-enabled, root-privileged user name | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| SSH-enabled, root-privileged Private Key Content PEM file | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
| VM IP addresses | [On-premises provider](../../../configure-yugabyte-platform/on-premises/) |
