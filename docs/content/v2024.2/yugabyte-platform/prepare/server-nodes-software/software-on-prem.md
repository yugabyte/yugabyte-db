---
title: YugabyteDB Anywhere node software requirements
headerTitle: Software requirements for on-premises nodes
linkTitle: On-premises provider
description: Software requirements for on-premises provider nodes.
headContent: Prepare a VM for deploying universes on-premises
menu:
  v2024.2_yugabyte-platform:
    identifier: software-on-prem
    parent: server-nodes-software
    weight: 20
type: docs
---

{{<tip>}}
For instructions for v2.20 and earlier, see [Create on-premises provider configuration](/v2.20/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/).
{{</tip>}}

When deploying database clusters using an on-premises provider, YugabyteDB Anywhere (YBA) relies on you to manually create the VMs and provide these pre-created VMs to YBA.

With the on-premises provider, you must provide to YBA one, three, five, or more VM(s) with the following installed:

- [Supported Linux OS](../#linux-os)
- [Additional software](../#additional-software)
- If you are not connected to the Internet, [additional software for airgapped](../#additional-software-for-airgapped-deployment)

## How to provision the nodes for use in a database cluster

After you have created the VMs, they must be provisioned with YugabyteDB and related software before they can be deployed in a universe.

### Node agent provisioning

The [YugabyteDB Anywhere Node agent](/preview/faq/yugabyte-platform/#what-is-a-node-agent) is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the nodes in universes. Node agent can also be used to provision on-premises nodes.

With this method, you:

1. Download the node agent package to the VM.
1. Set the configuration options in the provisioning configuration file.
1. With sudo permissions, run the provisioning script.

If you have already installed and are running YugabyteDB Anywhere, the node agent will additionally create or update an [on-premises provider](../../../configure-yugabyte-platform/on-premises/), as appropriate.

See [Node agent provisioning](../software-on-prem-na/).

### Classic provisioning

With classic provisioning, how you provision nodes for use with an on-premises provider depends on the SSH access that you can grant YugabyteDB Anywhere:

- If you can grant YugabyteDB Anywhere SSH access to nodes, YugabyteDB Anywhere can provision the nodes when you add nodes to the on-premises provider. You do this after installing YugabyteDB Anywhere and creating an on-premises provider.
- If you are unable to grant SSH access to the nodes, you must manually install each prerequisite software component. You can do this immediately after creating the VM.

| SSH mode | Description | Notes | For more details |
| :--- | :--- | :--- | :--- |
| Permissive | You can allow SSH to a root-privileged user, AND<br>You can provide YBA with SSH login credentials for that user. | For example, the ec2-user for AWS EC2 instances meets this requirement. In this mode, YugabyteDB Anywhere will sign in to the VM and automatically provision the nodes when you are adding instances to the on-premises provider. | See [Automatic Manual Provisioning](../software-on-prem-auto/). |
| Medium | You can allow SSH to a root-privileged user, AND<br>You can't provide YBA with SSH login credentials for that user; however you can enter the password interactively. | In this mode, you run a script on the VM, and are prompted for a password for each sudo action to install the required software. | See [Assisted Manual Provisioning](../software-on-prem-assist/). |
| Restrictive | All other cases (you disallow SSH login to a root-privileged user at setup time). | In this mode, you'll manually install each prerequisite software component. | See [Fully Manual Provisioning](../software-on-prem-manual/). |

Note that, for Permissive and Medium modes, the root-privileged SSH user can have any username except `yugabyte`. When YBA later uses this provided SSH user to sign in and prepare the node, YBA will automatically create and configure a second user – named `yugabyte` – which will be largely de-privileged, and will have very limited sudo commands available to it. YBA uses the `yugabyte` user to run the various YugabyteDB software processes.

SSH access is required for initial setup of a new database cluster (and when adding new nodes to the cluster). After setup is completed, SSH can be disabled, at your option. Leaving SSH enabled, however, is still recommended to provide access to nodes for troubleshooting.

## Best practices

When creating your VMs, create at least two virtual disks: one as the boot disk, and another for data and logs.
