---
title: Create on-premises provider configuration
headerTitle: Create the provider configuration
linkTitle: Create provider
description: Configure the on-premises provider configuration.
headContent: For deploying universes on your private cloud
menu:
  preview_yugabyte-platform:
    identifier: on-premises-provider
    parent: set-up-on-premises
    weight: 10
type: docs
---

Before you can deploy universes to private clouds using YugabyteDB Anywhere, you must create a provider configuration.

With on-premises providers, VMs are _not_ auto-created by YugabyteDB Anywhere; you must create a provider, manually create your VMs, and then add them to the provider's free pool of nodes.

Navigate to **Configs > Infrastructure > On-Premises Datacenters** to see a list of all currently configured on-premises providers.

## Create a provider

To create an on-premises provider:

1. Click **Create Config** to open the **OnPrem Provider Configuration** page.

    ![Create On-Premises provider](/images/yb-platform/config/yba-onp-config-create.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Create Provider Configuration** when you are done and wait for the configuration to complete.

After the provider is created, configure the provider hardware. Refer to [Configure hardware for YugabyteDB nodes](#configure-hardware-for-yugabytedb-nodes).

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

1. Click **Add Region**.

### SSH Key Pairs

In the **SSH User** field, enter the name of the user that has SSH privileges on your instances. This is required because YugabyteDB Anywhere needs SSH access to the nodes to provision them with YugabyteDB. Unless you plan to provision the database nodes manually, the SSH user needs to have password-free sudo permissions to complete a few tasks.

If the SSH user requires a password for sudo access or the SSH user does not have sudo access, you must enable the **Manually Provision Nodes** option (under **Advanced**) and [manually provision the instances](../on-premises-script/).

{{< tip title="SSH access" >}}
After you have provisioned and added the instances to the provider (including installing the node agent), YugabyteDB Anywhere no longer requires SSH or sudo access to nodes.
{{< /tip >}}

In the **SSH Port** field, provide the port number of SSH client connections.

In the **SSH Keypair Name** field, provide the name of the key pair.

Use the **SSH Private Key Content** field to upload the private key PEM file available to the SSH user for gaining access via SSH into your instances.

### Advanced

Disable the **DB Nodes have public internet access** option if you want the installation to run in an air-gapped mode without expecting any internet access.

YugabyteDB Anywhere uses the sudo user to set up YugabyteDB nodes. However, if any of the following statements are applicable to your use case, you need to enable the **Manually Provision Nodes** option:

- Pre-provisioned `yugabyte:yugabyte` user and group.
- Sudo user requires a password.
- The [SSH user](#ssh-key-pairs) is not a sudo user.

For manual provisioning, you are prompted to run a Python pre-provisioning script at a later stage to provision the database instances. Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).

Optionally, use the **YB Nodes Home Directory** field to specify the home directory of the `yugabyte` user. The default value is `/home/yugabyte`.

Enable **Install Node Exporter** if you want the node exporter installed. You can skip this step if you have node exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.

The **Node Exporter User** field allows you to override the default `prometheus` user. This is helpful when the user is pre-provisioned on nodes (when the user creation is disabled). If overridden, the installer checks whether or not the user exists and creates the user if it does not exist.

Use the **Node Exporter Port** field to specify the port number for the node exporter. The default value is 9300.

**NTP Setup** lets you to customize the Network Time Protocol server, as follows:

- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Next steps

- [Add nodes to the provider free pool](./on-premises-nodes/)