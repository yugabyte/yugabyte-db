---
title: Configure an on-premises provider configuration
headerTitle: Create on-premises provider configuration
linkTitle: On-premises
description: Configure the on-premises provider configuration.
headContent: For deploying universes on your private cloud
menu:
  v2.25_yugabyte-platform:
    identifier: set-up-on-premises
    parent: configure-yugabyte-platform
    weight: 10
type: docs
---

Before you can deploy universes to private clouds using YugabyteDB Anywhere (YBA), you must create an on-premises provider configuration.

With on-premises providers, VMs are _not_ auto-created by YBA; you must manually create your VMs, install the Linux operating system and additional software, provision them with YugabyteDB software, and then add them to the provider's free pool of nodes.

## Automatic provisioning

Using the YugabyteDB Anywhere node agent package, you can provision VMs, create an on-premises provider, and add the VMs to the provider.

Before provisioning nodes, ensure YugabyteDB Anywhere is [installed](../../install-yugabyte-platform/) and running.

1. Have your network administrator set up firewalls to open the ports required for YBA and the nodes to communicate. Refer to [Networking](../../prepare/networking/).
1. Have your system administrator create VMs that will be used as nodes in universes. This is typically done using your hypervisor or cloud provider. Do the following:

    - Locate the VMs in the regions and availability zones where you will be deploying universes.
    - Install a YugabyteDB-supported Linux OS on the VMs.

    For instructions on creating VMs that are suitable for deploying YugabyteDB, refer to [Software requirements for on-premises nodes](../../prepare/server-nodes-software/).

1. Have your system administrator provision the VMs. This requires:

    - Downloading the YugabyteDB Anywhere node agent package to the VM.
    - Modifying the configuration file.
    - Running the provisioning script (as root or via sudo).

    These steps prepare the node for use by YugabyteDB Anywhere.

    The provisioning script will additionally perform the following tasks (YugabyteDB Anywhere must be installed and running):

    - Create (or update) the on-premises provider.
    - Create the instance type.
    - Add the node instance to the provider.

    Refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

### Create a provider manually

_If the on-premises provider wasn't created when provisioning the VMs_, you can manually create the provider using the YugabyteDB Anywhere UI. Refer to [Create the provider configuration](../on-premises-provider/). Note: You must enable the **Manually Provision Nodes** option (under **Advanced**).

_If the instance type and instances weren't created when provisioning the VMs_, you can manually add the provisioned VMs to the provider. Obtain the IP addresses of the provisioned VMs from your system administrator. You need these to add the nodes to the provider. Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).

## Legacy provisioning

{{< warning title="Legacy provisioning deprecated" >}}

Legacy provisioning of on-premises nodes is deprecated, and v2025.2 (available late 2025) will not support legacy node provisioning. Before you can upgrade YugabyteDB Anywhere to v2025.2, all universes must be updated to use node agent and provisioned using [automatic provisioning](#automatic-provisioning).

{{< /warning >}}

To create, provision, and add nodes to your on-premises provider using legacy provisioning, you will perform tasks in roughly three stages.

<!--![Create on-premises provider](/images/yb-platform/config/yba-onprem-config-flow.png)-->

### Stage 1: Prepare your infrastructure

- Have your network administrator set up firewalls to open the ports required for YBA and the nodes to communicate. Refer to [Networking](../../prepare/networking/).
- Have your system administrator create VMs that will be used as nodes in universes. This is typically done using your hypervisor or cloud provider. Do the following:
  - Locate the VMs in the regions and availability zones where you will be deploying universes.
  - Install a YugabyteDB-supported Linux OS on the VMs.
  - Set up a `yugabyte` user with root privileges (SSH access and sudo-capable).

  For instructions on creating VMs that are suitable for deploying YugabyteDB, refer to [Legacy provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/).

### Stage 2: Create an on-premises provider configuration

In YBA, create an on-premises provider. This involves the following:

- Defining the regions and availability zones where the provider will be deploying universes.
- Providing SSH credentials for the `yugabyte` user.
- Providing NTP setup.
- If the SSH user does not have passwordless sudo access, enabling Manual provisioning for the provider.

Refer to [Create the provider configuration](../on-premises-provider/).

### Stage 3: Add nodes to the provider free pool

In YBA, navigate to the provider you created in Stage 2 and do the following:

1. Define instance types. An instance type defines some basic properties of the VMs you will be adding.
1. Provision the VMs. YBA supports 3 ways of provisioning nodes for running YugabyteDB depending upon the level of SSH access provided to YBA:

    | Provisioning | Description | What happens |
    | :--- | :--- | :--- |
    | Legacy automatic (deprecated) | YBA is provided an SSH user with sudo access for the nodes it needs to provision. For example, the `ec2-user` for AWS EC2 instances. | No action. YBA will automatically provision the VMs that you add. |
    | Legacy assisted&nbsp;manual (deprecated) | The SSH user requires a password for sudo access. | [Run a script](../on-premises-script/), provided by YBA, to provision each VM, providing credentials for the SSH user with sudo access. |
    | Legacy fully manual (deprecated) | Neither YBA nor the user has access to an SSH user with sudo access; only a local (non-SSH) user is available with sudo access. | Follow a sequence of steps to [provision each VM manually](../../prepare/server-nodes-software/software-on-prem-manual/) before adding the VM to the pool. |

1. Add the VMs (instances) to the provider.

1. Run pre-checks to validate the nodes you added.

Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).
