---
title: On-premises provisioning
headerTitle: On-premises provisioning
linkTitle: On-premises provisioning
description: Choose privileged or non-privileged access before preparing on-premises database nodes.
headContent: Choose how YugabyteDB Anywhere accesses on-premises nodes
aliases:
  - /stable/yugabyte-platform/prepare/server-nodes-software/software-on-prem-assist/
menu:
  stable_yugabyte-platform:
    identifier: on-prem-provisioning
    parent: server-nodes-software
    weight: 20
type: docs
---

## Choose access

YugabyteDB Anywhere does not create the VMs for an on-premises provider. You create them, then prepare them for YugabyteDB. What you do next depends on whether you give YugabyteDB Anywhere SSH access to those VMs.

| Access | [Provider settings](../../../configure-yugabyte-platform/on-premises/) | Who prepares the node |
| :--- | :--- | :--- |
| [Non-privileged](../software-on-prem/) | **SSH Key Pairs** empty. **Manually Provision Nodes** on. | You run `node-agent-provision.sh` on the VM. The script can create the provider and add the node. |
| [Privileged](../software-on-prem-auto/) | SSH user and private key set. **Manually Provision Nodes** off. | YugabyteDB Anywhere signs in and provisions the VM when you add it. |

Give YugabyteDB Anywhere SSH access when it should provision the nodes. When it must not sign in to the VMs, use the provisioning script.

A provider the script creates has **Manually Provision Nodes** on in the provider configuration. The script leaves a privileged provider unchanged: if **Manually Provision Nodes** is off, the script stops.

To prepare a node yourself, without the script and without giving YugabyteDB Anywhere SSH access, follow [Prepare nodes yourself](../software-on-prem-manual/). Use that path when you have your own automation for managing your nodes. Otherwise, for a provider with no SSH access, use the provisioning script.

You can upgrade YugabyteDB Anywhere without changing your provisioning method. Universes already using privileged access, the provisioning script, or hand preparation keep working. Refer to [Prepare to upgrade](../../../upgrade/prepare-to-upgrade/).

## Best practices

When you create the VMs, create at least two virtual disks: one as the boot disk, and another for data and logs.
