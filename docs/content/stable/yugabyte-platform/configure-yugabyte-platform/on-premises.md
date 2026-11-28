---
title: Configure an on-premises provider configuration
headerTitle: On-premises provider configurations
linkTitle: On-premises
description: Manage on-premises provider configurations.
headContent: For deploying universes on your private cloud
aliases:
  - /stable/deploy/enterprise-edition/configure-cloud-providers/onprem
menu:
  stable_yugabyte-platform:
    identifier: set-up-on-premises
    parent: configure-yugabyte-platform
    weight: 10
type: docs
---

Before you can deploy universes to private clouds using YugabyteDB Anywhere, you must have an on-premises provider configuration.

With on-premises providers, VMs are _not_ auto-created by YugabyteDB Anywhere. You create the VMs, install the Linux operating system and additional software, prepare them for YugabyteDB, and add them to the provider's free pool of nodes.

How you prepare the VMs depends on whether you give YugabyteDB Anywhere SSH access. Refer to [On-premises provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/).

## Non-privileged access

You do not give YugabyteDB Anywhere SSH credentials. On each VM, the provisioning script prepares the node, and can create the provider and add the VM.

Before provisioning nodes, ensure YugabyteDB Anywhere is [installed](../../install-yugabyte-platform/) and running.

1. Have your network administrator set up firewalls to open the ports required for YugabyteDB Anywhere and the nodes to communicate. Refer to [Networking](../../prepare/networking/).
1. Have your system administrator create VMs that will be used as nodes in universes. This is typically done using your hypervisor or cloud provider. Do the following:

    - Locate the VMs in the regions and availability zones where you will be deploying universes.
    - Install a YugabyteDB-supported Linux OS on the VMs.

    For instructions on creating VMs that are suitable for deploying YugabyteDB, refer to [Software requirements for database nodes](../../prepare/server-nodes-software/).

1. Have your system administrator provision the VMs. This requires:

    - Downloading the YugabyteDB Anywhere node agent package to the VM.
    - Modifying the configuration file.
    - Running the provisioning script (as root or via sudo).

    These steps prepare the node for use by YugabyteDB Anywhere. When YugabyteDB Anywhere is installed and running, the script also does the following:

    - Creates the on-premises provider with **Manually Provision Nodes** on, or adds the node to an existing provider that already has that option on. (A provider with **Manually Provision Nodes** off is left unchanged, and the script stops.)
    - Creates the instance type, when you are not reusing one.
    - Adds the node instance to the provider.

    Refer to [Automatic provisioning without privileged access](../../prepare/server-nodes-software/software-on-prem/).

### Create a provider manually

If the script did not create the provider, create it in YugabyteDB Anywhere. Refer to [Manage the provider configuration](../on-premises-provider/). Turn **Manually Provision Nodes** on (under **Advanced**), and leave **SSH Key Pairs** empty.

If the script did not add the instance, add the VM yourself. Obtain the IP addresses from your system administrator. Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).

## Privileged access

You give YugabyteDB Anywhere an SSH user with passwordless sudo. YugabyteDB Anywhere provisions each VM when you add it to the provider. (This was formerly called legacy automatic provisioning.)

### Prepare your infrastructure

- Have your network administrator set up firewalls to open the ports required for YugabyteDB Anywhere and the nodes to communicate. Refer to [Networking](../../prepare/networking/).
- Have your system administrator create VMs that will be used as nodes in universes. This is typically done using your hypervisor or cloud provider. Do the following:
  - Locate the VMs in the regions and availability zones where you will be deploying universes.
  - Install a YugabyteDB-supported Linux OS on the VMs.
  - Create an SSH user with passwordless sudo. The user must not be named `yugabyte`.

  Refer to [Automatic provisioning with privileged access](../../prepare/server-nodes-software/software-on-prem-auto/).

### Create an on-premises provider configuration

In YugabyteDB Anywhere, create an on-premises provider. This involves the following:

- Defining the regions and availability zones where the provider will be deploying universes.
- Providing the SSH user and private key.
- Providing NTP setup.
- Leaving **Manually Provision Nodes** off.

Refer to [Create the provider configuration](../on-premises-provider/).

### Add nodes to the provider free pool

In YugabyteDB Anywhere, navigate to the provider and do the following:

1. Define instance types. An instance type describes the VMs you will add.
1. Add the VMs (instances). YugabyteDB Anywhere provisions them, including installing node agent.
1. Run pre-checks to validate the nodes you added.

Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).

## Prepare nodes yourself

When YugabyteDB Anywhere has no SSH access and you will not [run the provisioning script](#non-privileged-access), prepare each VM by hand before you add it. Leave **SSH Key Pairs** empty and turn **Manually Provision Nodes** on. (This was formerly referred to as Legacy manual provisioning.)

Refer to [Prepare nodes yourself](../../prepare/server-nodes-software/software-on-prem-manual/). Use this when you already automate node preparation. Otherwise use the provisioning script.
