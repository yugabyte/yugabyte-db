---
title: Configure the on-premises cloud provider
headerTitle: Create provider configuration
linkTitle: Create provider configuration
description: Configure the on-premises provider configuration.
headContent: Configure an on-premises provider configuration
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/onprem
menu:
  preview_yugabyte-platform:
    identifier: set-up-cloud-provider-6-on-premises
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      OpenShift
    </a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

Before you can deploy universes using YugabyteDB Anywhere, you must create a provider configuration.

A provider configuration describes your cloud environment (such as its regions and availability zones, NTP server, certificates that may be used to SSH to VMs, whether YugabyteDB database software will be manually installed by the user or auto-provisioned by YugabyteDB Anywhere, and so on). The provider configuration is used as an input when deploying a universe, and can be reused for many universes.

With on-premises providers, VMs are _not_ auto-created by YugabyteDB Anywhere; you must manually create and add them to the free pool of the on-premises provider. Only after VM instances are added can YugabyteDB Anywhere auto-provision or can you manually provision the YugabyteDB database software and create universes from these database nodes.

Creating an on-premises provider requires the following steps:

- Create your VMs. You do this using your hypervisor or cloud provider.
- Create the on-premises provider configuration.
- Configure the hardware for the node instances that the provider will use for deploying YugabyteDB universes, including
  - Adding instance types
  - Adding instances

## Configure the on-premises provider

Navigate to **Configs > Infrastructure > On-Premises Datacenters** to see a list of all currently configured on-premises providers.

### View and edit providers

To view a provider, select it in the list of On Prem Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). Note that, depending on whether the provider has been used to create a universe, you can only edit a subset of options.

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

### Create a provider

To create an on-premises provider:

1. Click **Create Config** to open the **OnPrem Provider Configuration** page.

    ![Create On-Premises provider](/images/yb-platform/config/yba-onp-config-create.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Create Provider Configuration** when you are done and wait for the configuration to complete.

After the provider is created, configure the provider hardware. Refer to [Configure hardware for YugabyteDB nodes](#configure-hardware-for-yugabytedb-nodes).

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

In the **SSH User** field, enter the name of the user that has SSH privileges on your instances. This is required because to provision on-premises nodes with YugabyteDB, YugabyteDB Anywhere needs SSH access to these nodes. Unless you plan to provision the database nodes manually, the user needs to have password-free sudo permissions to complete a few tasks.

If the SSH user requires a password for sudo access or the SSH user does not have sudo access, follow the steps described in [Provision nodes manually](#provision-nodes-manually).

In the **SSH Port** field, provide the port number of SSH client connections.

In the **SSH Keypair Name** field, provide the name of the key pair.

Use the **SSH Private Key Content** field to upload the private key PEM file available to the SSH user for gaining access via SSH into your instances.

### Advanced

Disable the **DB Nodes have public internet access** option if you want the installation to run in an air-gapped mode without expecting any internet access.

YugabyteDB Anywhere uses the sudo user to set up YugabyteDB nodes. However, if any of the following statements are applicable to your use case, you need to enable the **Manually Provision Nodes** option:

- Pre-provisioned `yugabyte:yugabyte` user and group.
- Sudo user requires a password.
- The SSH user is not a sudo user.

For manual provisioning, you are prompted to run a Python provisioning script at a later stage to provision the database instances (refer to [Provision nodes manually](#provision-nodes-manually)).

Optionally, use the **YB Nodes Home Directory** field to specify the home directory of the `yugabyte` user. The default value is `/home/yugabyte`.

Enable **Install Node Exporter** if you want the node exporter installed. You can skip this step if you have node exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.

The **Node Exporter User** field allows you to override the default `prometheus` user. This is helpful when the user is pre-provisioned on nodes (when the user creation is disabled). If overridden, the installer checks whether or not the user exists and creates the user if it does not exist.

Use the **Node Exporter Port** field to specify the port number for the node exporter. The default value is 9300.

**NTP Setup** lets you to customize the Network Time Protocol server, as follows:

- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Configure hardware for YugabyteDB nodes

After the provider has been created, you can configure the hardware for the on-premises configuration by navigating to **Configs > Infrastructure > On-Premises Datacenters**, selecting the on-prem configuration you created, and choosing **Instances**. This displays the configured instance types and instances for the selected provider.

![On-prem pre-provisioning script](/images/yb-platform/config/yba-onprem-config-script.png)

To configure the hardware, do the following:

1. [Add instance types](#add-instance-types).
2. Add instances.
    - If you are not manually provisioning nodes, [add instances](#add-instances) for each node.
    - If the **Manually Provision Nodes** option is enabled for the provider configuration, first [provision the nodes manually](#provision-nodes-manually), then add instances.

### Add instance types

To add an instance type, do the following:

1. Click **Add Instance Type**.

1. Complete the **Add Instance Type** dialog fields, as follows:

    - Use the **Machine Type** field to define a value to be used internally as an identifier in the **Instance Type** universe field.
    - Use the **Number of Cores** field to define the number of cores to be assigned to a node.
    - Use the **Memory Size (GB)** field to define the memory allocation of a node.
    - Use the **Volume Size (GB)** field to define the disk volume of a node.
    - Use the **Mount Paths** field to define a mount point with enough space to contain your node density. Use `/data`. If you have multiple drives, add these as a comma-separated list, such as, for example, `/mnt/d0,/mnt/d1`.

1. Click **Add Instance Type**.

### Add instances

You can add instances to an on-prem provider using the YugabyteDB Anywhere UI.

If **Manually Provision Nodes** is enabled in the on-prem provider configuration, you must [manually provision instances](#provision-nodes-manually) before adding them.

#### Prerequisites

Before you add instances, you need the following:

- The IP addresses of your VMs. Before you can add instances, you need to create your VMs. You do this using your hypervisor or cloud provider.
- Instance type to assign each instance. The instance types define properties of the instances, along with the mount points. See [Add instance types](#add-instance-types).

#### Add instances in YugabyteDB Anywhere

To add the instances, do the following:

1. Click **Add Instances**.

    ![On-prem Add Instance Types dialog](/images/yb-platform/config/yba-onprem-config-add-instances.png)

1. For each node in each region, provide the following:

    - Select the zone.
    - Select the instance type.
    - Enter the IP address of the node. You can use DNS names or IP addresses when adding instances.
    - Optionally, enter an Instance ID; this is a user-defined identifier.

1. Click **+ Add** to add additional nodes in a region.

1. Click **Add** when you are done.

Note that if you provide a hostname, the universe might experience issues communicating. To resolve this, you need to delete the failed universe and then recreate it with the `use_node_hostname_for_local_tserver` flag enabled.

This completes the on-premises cloud provider configuration. You can proceed to [Configure the backup target](../../backup-target/) or [Create deployments](../../../create-deployments/).

### Provision nodes manually

When provisioning nodes manually, you will follow one of two procedures, depending on the privileges of your SSH user:

- SSH user has sudo privileges

    Follow the instructions in [Provision nodes using the pre-provisioning script](#provision-nodes-manually-using-the-pre-provisioning-script). You run the pre-provisioning script on each node to install the YugabyteDB software and node agent.

- SSH user does not have sudo privileges |

    Follow the instructions in [Set up on-premises nodes manually](../on-premises-manual).

Note that after you have provisioned nodes, including installing the node agent, YugabyteDB Anywhere no longer requires SSH or sudo access to nodes.

#### Provision nodes manually using the pre-provisioning script

This step is only required if you set **Manually Provision Nodes** to true on your on-prem provider configuration, and the SSH user has sudo privileges which require a password.

{{< note title="Note" >}}
If the SSH user does not have any sudo privileges, you can't use the script and need to set up the database nodes manually. Refer to [Set up on-premises nodes manually](../on-premises-manual/).
{{< /note >}}

To provision your nodes you can run the pre-provisioning script. The script is displayed under **Instances** on the **Instances** tab of the on-prem configuration you created.

You can manually provision each node using the pre-provisioning Python script, as follows:

1. Log in to the YugabyteDB Anywhere virtual machine via SSH.

1. Access the Docker `yugaware` container, as follows:

    ```sh
    sudo docker exec -it yugaware bash
    ```

1. Copy and paste the Python script command from the YugabyteDB Anywhere UI. Set the flags for the command as follows:

    - `--ask_password` - this flag instructs the script to prompt for a password (which is required if the sudo user requires password authentication).
    - `--install_node_agent` - this flag instructs the script to install the node agent, which is required for YugabyteDB Anywhere to communicate with the instance.
    - `--yba_url` - enter the IP address of the machine where you are running YugabyteDB Anywhere, with the port of 9000.
    - `--api_token` - enter your API token; you can create an API token by navigating to your **User Profile** and clicking **Generate Key**.
    - `--node_name` - enter a name for the node.
    - `--instance_type` - enter the name of the [instance type](#add-instance-types) to use for the node.
    - `--zone_name` - enter the name of the zone where the node is located.
    - `--ip` - enter the IP address of the node.
    - `--mount_points` - enter the mount point configured for the node (typically `/data`)

    For example:

    ```bash
    /opt/yugabyte/yugaware/data/provision/9cf26f3b-4c7c-451a-880d-593f2f76efce/provision_instance.py \
        --ask_password --install_node_agent \
        --api_token 999bc9db-ddfb-9fec-a33d-4f8f9fd88db7 \
        --yba_url http://100.98.0.40:9000 \
        --ip 10.9.116.65 \
        --node_name onprem_node1 \
        --instance_type c5.large \
        --zone_name us-west-2a 
        --mount_points /data \
    ```

    Expect the following output and, if you specified `--ask-password`, prompt:

    ```output
    Executing provision now for instance with IP 10.9.116.65...
    SUDO password:
    ```

1. Enter your password.

1. Wait for the script to finish successfully.

1. Repeat step 3 for every node that will participate in the universe.

This completes the on-premises cloud provider configuration. You can proceed to [Configure the backup target](../../backup-target/) or [Create deployments](../../../create-deployments/).

## Use node agents

To automate some of the steps outlined in [Provision nodes manually](#provision-nodes-manually), YugabyteDB Anywhere provides a node agent that runs on each node meeting the following requirements:

- The node has already been set up with the `yugabyte` user group and home.
- The bi-directional communication between the node and YugabyteDB Anywhere has been established (that is, the IP address can reach the host and vice versa).

The node agents are installed onto instances automatically when adding instances or running the pre-provisioning script using the `--install_node_agent` flag.

You can also install the node agent manually.

### Install node agent manually

You can install a node agent manually as follows:

1. Download the installer from YugabyteDB Anywhere using the API token of the Super Admin, as follows:

   ```sh
   curl https://<yugabytedb_anywhere_address>/api/v1/node_agents/download --fail --header 'X-AUTH-YW-API-TOKEN: <api_token>' > installer.sh && chmod +x installer.sh
   ```

1. Verify that the installer file contains the script.

1. Run the following command to download the node agent's `.tgz` file which installs and starts the interactive configuration:

   ```sh
   ./installer.sh -c install -u https://<yugabytedb_anywhere_address> -t <api_token>
   ```

   For example, if you execute `./installer.sh  -c install -u http://100.98.0.42:9000 -t 301fc382-cf06-4a1b-b5ef-0c8c45273aef`, expect the following output:

   ```output
   * Starting YB Node Agent install
   * Creating Node Agent Directory
   * Changing directory to node agent
   * Creating Sub Directories
   * Downloading YB Node Agent build package
   * Getting Linux/amd64 package
   * Downloaded Version - 2.17.1.0-PRE_RELEASE
   * Extracting the build package
   * The current value of Node IP is not set; Enter new value or enter to skip: 10.9.198.2
   * The current value of Node Name is not set; Enter new value or enter to skip: Test
   * Select your Onprem Provider
   1. Provider ID: 41ac964d-1db2-413e-a517-2a8d840ff5cd, Provider Name: onprem
           Enter the option number: 1
   * Select your Instance Type
   1. Instance Code: c5.large
           Enter the option number: 1
   * Select your Region
   1. Region ID: dc0298f6-21bf-4f90-b061-9c81ed30f79f, Region Code: us-west-2
           Enter the option number: 1
   * Select your Zone
   1. Zone ID: 99c66b32-deb4-49be-85f9-c3ef3a6e04bc, Zone Name: us-west-2c
           Enter the option number: 1
           • Completed Node Agent Configuration
           • Node Agent Registration Successful
   You can install a systemd service on linux machines by running sudo node-agent-installer.sh -c install-service --user yugabyte (Requires sudo access).
   ```

1. Run the following command to enable the node agent as a systemd service, which is required for self-upgrade and other functions:

   ```sh
   sudo node-agent-installer.sh -c install-service --user yugabyte
   ```

When the installation has been completed, the configurations are saved in the `config.yml` file located in the `node-agent/config/` directory. You should refrain from manually changing values in this file.

### Manual registration

To enable secured communication, the node agent is automatically registered during its installation so YugabyteDB Anywhere is aware of its existence. You can also register and unregister the node agent manually during configuration.

The following is the node agent registration command:

```sh
node-agent node register --api-token <api_token>
```

If you need to overwrite any previously configured values, you can use the following parameters in the registration command:

- `--node_ip` represents the node IP address.
- `--url` represents the YugabyteDB Anywhere address.

For secured communication, YugabyteDB Anywhere generates a key pair (private, public, and server certificate) that is sent to the node agent as part of its registration process.

<!--

You can obtain a list of existing node agents using the following API:

```http
GET /api/v1/customers/<customer_id>/node_agents
```

To unregister a node agent, use the following API:

```http
DELETE /api/v1/customers/<customer_id>/node_agents/<node_agent_id>
```

-->

To unregister a node agent, use the following command:

```sh
node-agent node unregister
```

### Node agent operations

Even though the node agent installation, configuration, and registration are sufficient, the following supplementary commands are also supported:

- `node-agent node unregister` is used for un-registering the node and node agent from YugabyteDB Anywhere. This can be done to restart the registration process.
- `node-agent node register` is used for registering a node and node agent to YugabyteDB Anywhere if they were unregistered manually. Registering an already registered node agent fails as YugabyteDB Anywhere keeps a record of the node agent with this IP.
- `node-agent service start` and `node-agent service stop` are used for starting or stopping the node agent as a gRPC server.
- `node-agent node preflight-check` is used for checking if a node is configured as a YugabyteDB Anywhere node. After the node agent and the node have been registered with YugabyteDB Anywhere, this command can be run on its own, if the result needs to be published to YugabyteDB Anywhere. For more information, see [Preflight check](#preflight-check).

### Preflight check

After the node agent is installed, configured, and connected to YugabyteDB Anywhere, you can perform a series of preflight checks without sudo privileges by using the following command:

```sh
node-agent node preflight-check
```

The result of the check is forwarded to YugabyteDB Anywhere for validation. The validated information is posted in a tabular form on the terminal. If there is a failure against a required check, you can apply a fix and then rerun the preflight check.

Expect an output similar to the following:

![Result](/images/yp/node-agent-preflight-check.png)

If the preflight check is successful, you would be able to add the node to the provider (if required) by executing the following:

```sh
node-agent node preflight-check --add_node
```
