---
title: Automatic provisioning with privileged access
headerTitle: Automatic provisioning with privileged access
linkTitle: Privileged
description: Prepare on-premises database nodes when YugabyteDB Anywhere has SSH access.
headContent: Prepare a VM when YugabyteDB Anywhere has SSH access to it
menu:
  stable_yugabyte-platform:
    identifier: software-on-prem-2-auto
    parent: on-prem-provisioning
    weight: 20
type: docs
---

{{< note title="Privileged access" >}}
Use these instructions when you give YugabyteDB Anywhere SSH access to the provider VMs. In the provider configuration, set the [SSH user and private key](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs), and leave [**Manually Provision Nodes**](../../../configure-yugabyte-platform/on-premises-provider/#manually-provision-nodes) off.

(This provisioning method was formerly called legacy automatic provisioning.)

When YugabyteDB Anywhere must not have SSH access, prepare the node using the provisioning script. See [Automatic provisioning without privileged access](../software-on-prem/).
{{< /note >}}

Give YugabyteDB Anywhere an SSH user with passwordless sudo (for example, `ec2-user` on AWS EC2). YugabyteDB Anywhere signs in as that user and provisions the VM when you add the instance to the provider.

The SSH user must not be named `yugabyte`. YugabyteDB Anywhere creates a second user named `yugabyte` to run the database processes. That user has a short list of sudo commands.

SSH access is required while YugabyteDB Anywhere provisions a node, and when you add nodes later. After provisioning, you can remove SSH access. Leaving it in place is still useful for troubleshooting.

In the provider, enter the SSH user and private key, and leave **Manually Provision Nodes** off. Refer to [SSH Key Pairs](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs).

## With Internet or Yum connectivity

If your VM has Internet or Yum connectivity, provide YugabyteDB Anywhere a VM with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH user that has passwordless sudo. YugabyteDB Anywhere uses this user to finish Linux configuration, including creating the `yugabyte` user and setting file descriptor limits.
- [Additional software](../#additional-software)

Prepare the VM now.

- Save the SSH user name and the SSH private key PEM file.
- Save the VM IP addresses for when you create the on-premises provider.

| Save for later | To configure |
| :--- | :--- |
| SSH user name | [On-premises provider](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs) |
| SSH private key PEM file | [On-premises provider](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs) |
| VM IP addresses | [On-premises provider nodes](../../../configure-yugabyte-platform/on-premises-nodes/) |

## No Internet nor Yum connectivity

If your VM has neither Internet nor Yum connectivity, provide YugabyteDB Anywhere a VM with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH user that has passwordless sudo. YugabyteDB Anywhere uses this user to finish Linux configuration, including creating the `yugabyte` user and setting file descriptor limits.
- [Additional software](../#additional-software)
- [Additional software for airgapped](../#additional-software-for-airgapped-deployment)

Prepare the VM now.

- Save the SSH user name and the SSH private key PEM file.
- Save the VM IP or DNS addresses for when you create the on-premises provider.

| Save for later | To configure |
| :--- | :--- |
| SSH user name | [On-premises provider](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs) |
| SSH private key PEM file | [On-premises provider](../../../configure-yugabyte-platform/on-premises-provider/#ssh-key-pairs) |
| VM IP addresses | [On-premises provider nodes](../../../configure-yugabyte-platform/on-premises-nodes/) |
