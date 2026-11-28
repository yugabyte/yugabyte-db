---
title: Manage your on-premises provider configurations
headerTitle: Manage the provider configuration
linkTitle: Manage provider
description: Manage your on-premises provider configurations.
headContent: For deploying universes on your private cloud
menu:
  stable_yugabyte-platform:
    identifier: on-premises-provider
    parent: set-up-on-premises
    weight: 10
type: docs
---

Navigate to **Integrations > Infrastructure > On-Premises Datacenters** to see a list of all currently configured on-premises providers.

Before you can deploy universes to private clouds using YugabyteDB Anywhere, you must have an on-premises provider configuration. YugabyteDB Anywhere does not create the VMs. You create the provider, create the VMs, and add them to the provider's free pool of nodes.

With [non-privileged access](../../prepare/server-nodes-software/software-on-prem/), the provisioning script can create the provider when it prepares a node. With [privileged access](../../prepare/server-nodes-software/software-on-prem-auto/), you create the provider and supply SSH credentials so YugabyteDB Anywhere can provision the VMs.

## Create a provider

Create an on-premises provider as follows:

1. Click **Create Config** to open the **OnPrem Provider Configuration** page.

    ![Create On-Premises provider](/images/yb-platform/config/yba-onp-config-create.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Create Provider Configuration** when you are done and wait for the configuration to complete.

After the provider is created, add your VMs to the provider's free pool of nodes. Refer to [Add nodes to the provider free pool](../on-premises-nodes/).

## View and edit providers

To view a provider, select it in the list of On Prem Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). Note that, depending on whether the provider has been used to create a universe, you can only edit a subset of options.

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

## Provider settings

### Provider Name

Enter a Provider name. The Provider name is an internal tag used for organizing cloud providers.

### Regions

To add regions for the provider, do the following:

1. Click **Add Region**.

1. Enter a name for the region.

1. Select the region location.

1. To add a zone, click **Add Zone** and enter a name for the zone.

    {{<tip title="Rack awareness">}}
For on-premises deployments, consider racks as zones to treat them as fault domains.
    {{</tip>}}

1. Click **Add Region**.

### SSH Key Pairs

Set these fields for [privileged access](../../prepare/server-nodes-software/software-on-prem-auto/). YugabyteDB Anywhere uses them to sign in and provision the VMs. Leave them empty for [non-privileged access](../../prepare/server-nodes-software/software-on-prem/).

In the **SSH User** field, enter the name of a user with passwordless sudo on your instances. This SSH user cannot be named `yugabyte`. YugabyteDB Anywhere signs in as the user you enter here, then creates a `yugabyte` user to run the database processes.

Leave **Manually Provision Nodes** off (under **Advanced**). (Off is the default.)

In the **SSH Port** field, provide the port number of SSH client connections.

In the **SSH Keypair Name** field, provide the name of the key pair.

Use the **SSH Private Key Content** field to upload the private key PEM file for that SSH user.

### Advanced

#### DB Nodes have public internet access

Disable this option if you want the installation to run in an airgapped mode without expecting any internet access.

#### Manually Provision Nodes

Controls whether YugabyteDB Anywhere provisions the VMs:

- Leave this _off_ for [privileged access](../../prepare/server-nodes-software/software-on-prem-auto/). You set [SSH Key Pairs](#ssh-key-pairs), and YugabyteDB Anywhere provisions each VM when you add it. This is the default.
- Turn this _on_ for [non-privileged access](../../prepare/server-nodes-software/software-on-prem/). Leave **SSH Key Pairs** empty. Prepare the node with `node-agent-provision.sh`, or [prepare it yourself](../../prepare/server-nodes-software/software-on-prem-manual/).

A provider created by `node-agent-provision.sh` has this option on. If the provider already exists and this option is off, the script leaves the provider unchanged, and stops.

#### YB Nodes Home Directory

Optionally, use the **YB Nodes Home Directory** field to specify the home directory of the `yugabyte` user. The default value is `/home/yugabyte`.

#### Install Node Exporter

Enable this option if you want the Prometheus Node Exporter installed. You can skip this step if you have Node Exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.

The **Node Exporter User** field allows you to override the default `prometheus` user. This is helpful when the user is pre-provisioned on nodes (when the user creation is disabled). If overridden, the installer checks whether or not the user exists and creates the user if it does not exist.

Use the **Node Exporter Port** field to specify the port number for the Prometheus Node Exporter. The default value is 9300.

#### NTP Setup

You can customize the Network Time Protocol server:

- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Next step

- [Add nodes to the provider free pool](../on-premises-nodes/)
