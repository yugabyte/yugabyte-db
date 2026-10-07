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

If you are using automatic provisioning, on-premises providers are created automatically when provisioning nodes. Refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

Before you can deploy universes to private clouds using YugabyteDB Anywhere, you must have an on-premises provider configuration. With on-premises providers, VMs are _not_ auto-created by YugabyteDB Anywhere; you must create a provider, manually create your VMs, and then add them to the provider's free pool of nodes.

## Create a provider

{{< tip title="Automatic provisioning" >}}

If you are using automatic provisioning, on-premises providers are created automatically when provisioning nodes. Refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

{{< /tip >}}

You can create an on-premises provider manually as follows:

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

Required for [legacy automatic provisioning](../../prepare/server-nodes-software/software-on-prem-auto/), to provide sudo access to VMs.

In the **SSH User** field, enter the name of the user that has SSH privileges on your instances. This SSH user cannot be named `yugabyte`.

YugabyteDB Anywhere will use this user for SSH access to the nodes in order to provision them. In addition, deselect the **Manually Provision Nodes** option (under **Advanced**) (the default).

In the **SSH Port** field, provide the port number of SSH client connections.

In the **SSH Keypair Name** field, provide the name of the key pair.

Use the **SSH Private Key Content** field to upload the private key PEM file available to the SSH user for gaining access via SSH into your instances.

{{< tip title="SSH access" >}}
Passwordless sudo SSH is legacy automatic provisioning: **Manually Provision Nodes** stays off, and YugabyteDB Anywhere provisions the nodes. Leave these fields empty when you use the [node agent provisioning script](../../prepare/server-nodes-software/software-on-prem/) (recommended when YugabyteDB Anywhere has no sudo). To learn which provisioning method you are using, see [Choose a provisioning method](../../prepare/server-nodes-software/software-on-prem/#choose-a-provisioning-method).
{{< /tip >}}

### Advanced

DB Nodes have public internet access
: Disable this option if you want the installation to run in an airgapped mode without expecting any internet access.

Manually Provision Nodes
: Indicates whether you are providing [SSH Key Pairs](#ssh-key-pairs) (sudo privileges) to YugabyteDB Anywhere for it to automatically manage VM provisioning. This toggle is how you tell the two go-forward paths apart. For information about when to run the provisioning script, see [When to use the provisioning script](../../prepare/server-nodes-software/software-on-prem/#when-to-use-the-provisioning-script).
: **Off.** Provide [SSH Key Pairs](#ssh-key-pairs) for a user with passwordless sudo. YugabyteDB Anywhere uses that key to sign in and provision the nodes ([legacy automatic provisioning](../../prepare/server-nodes-software/software-on-prem-auto/)). Running `node-agent-provision.sh` on these database nodes is unsupported. Re-apply OS settings with [Reprovision Universe Nodes](../../manage-deployments/reprovision-nodes/) (v2026.1.2.0 and later).
: **On.** YugabyteDB Anywhere has no sudo SSH key. Prepare nodes with the [node agent provisioning script](../../prepare/server-nodes-software/software-on-prem/). The script selects this option when it creates or updates the provider. [Legacy fully manual](../../prepare/server-nodes-software/software-on-prem-manual/) provisioning also uses this setting.

YB Nodes Home Directory
: Optionally, use the **YB Nodes Home Directory** field to specify the home directory of the `yugabyte` user. The default value is `/home/yugabyte`.

Install Node Exporter
: Enable this option if you want the Prometheus Node Exporter installed when YugabyteDB Anywhere provisions the node (**Manually Provision Nodes** off). You can skip this step if you have Node Exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.
: On the script path (**Manually Provision Nodes** on), `node-agent-provision.sh` installs the bundled node exporter during the root modules and restarts `node_exporter.service` on `node_exporter_port`. This provider setting does not skip that install. For information about what the script installs, see [What the script configures](../../prepare/server-nodes-software/software-on-prem/#what-the-script-configures).
: The **Node Exporter User** field allows you to override the default `prometheus` user. This is helpful when the user is pre-provisioned on nodes (when the user creation is disabled). If overridden, the installer checks whether or not the user exists and creates the user if it does not exist.
: Use the **Node Exporter Port** field to specify the port number for the Prometheus Node Exporter. The default value is 9300.

NTP Setup
: You can customize the Network Time Protocol server.
: Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
: Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Next step

- Stage 3: [Add nodes to the provider free pool](../on-premises-nodes/)
