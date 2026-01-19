---
title: Requirements for servers for database nodes using on-premises providers
headerTitle: Automatically provision database nodes for on-premises providers
linkTitle: On-premises provider
description: Prepare a VM for deploying universes on-premises.
headContent: Prepare a VM for deploying universes on-premises
menu:
  v2025.1_yugabyte-platform:
    identifier: software-on-prem
    parent: server-nodes-software
    weight: 20
type: docs
rightNav:
  hideH4: true
---

When deploying universes using an on-premises provider, YugabyteDB Anywhere relies on you to manually create the VMs for the database nodes and provide these pre-created VMs to YugabyteDB Anywhere.

## Prerequisites

- Provide one, three, five, or more VM(s) with the following installed:
  - [Supported Linux OS](../#linux-os)
  - [Additional software](../#additional-software)
  - If you are not connected to the Internet, [additional software for airgapped](../#additional-software-for-airgapped-deployment)

- YugabyteDB Anywhere is [installed and running](../../../install-yugabyte-platform/).

## How to prepare the nodes for use in a database cluster

After you have created the VMs with the operating system and additional software, you must further prepare the VMs as follows:

1. Download the YugabyteDB Anywhere node agent package to the VM.
1. Modify the configuration file.
1. Run the provisioning script (as root or via sudo).

These steps prepare the node for use by YugabyteDB Anywhere, including setting ulimits and transparent hugepages. If you have already [installed YugabyteDB Anywhere](../../../install-yugabyte-platform/) and it is running (recommended), the script additionally creates (or updates) an [on-premises provider](../../../configure-yugabyte-platform/on-premises/) with the node already added.

Root or sudo privileges are only required to provision the nodes. After the node is provisioned (with [YugabyteDB Anywhere node agent](/stable/faq/yugabyte-platform/#what-is-a-node-agent) installed), sudo is no longer required.

### Download the package

To begin, download the YugabyteDB Anywhere node agent package to the node you want to provision.

#### Use the API

If you have already installed YugabyteDB Anywhere and it is running, you can download the node agent package from YugabyteDB Anywhere using the [Download node agent API](https://api-docs.yugabyte.com/docs/yugabyte-platform/22174ba86f880-download-node-agent-installer-or-package).

```sh
curl -k https://<yba_address>/api/v1/node_agents/download\?downloadType\=package\&os\=LINUX\&arch\=AMD64 --fail --header 'X-AUTH-YW-API-TOKEN: <api_token>'  > node-agent.tar.gz
```

- `<yba_address>` is the address of your YugabyteDB Anywhere installation.
- `<api_token>` is an API token you created. For information on creating an API token, refer to [API authentication](../../../anywhere-automation/#authentication).
- You can change the architecture from AMD64 to ARM64 as appropriate.

Use this method if you don't have internet connectivity. This downloads the same version of node agent as the version of YugabyteDB Anywhere you are running.

Extract the package and go to the `scripts` directory.

```sh
tar -xvzf node-agent.tar.gz && cd {{<yb-version version="v2025.1" format="build">}}/scripts/
```

#### Direct download

Alternatively, the node agent package is included in the YBA Installer package. Download and extract the YBA Installer by entering the following commands:

```sh
wget https://downloads.yugabyte.com/releases/{{<yb-version version="v2025.1" format="long">}}/yba_installer_full-{{<yb-version version="v2025.1" format="build">}}-linux-x86_64.tar.gz
tar -xf yba_installer_full-{{<yb-version version="v2025.1" format="build">}}-linux-x86_64.tar.gz
cd yba_installer_full-{{<yb-version version="v2025.1" format="build">}}/
```

Extract the yugabundle package:

```sh
tar -xf yugabundle-{{<yb-version version="v2025.1" format="build">}}-centos-x86_64.tar.gz
cd yugabyte-{{<yb-version version="v2025.1" format="build">}}/
```

Extract the node agent package and go to the `scripts` directory:

```sh
tar -xf node_agent-{{<yb-version version="v2025.1" format="build">}}-linux-amd64.tar.gz && cd {{<yb-version version="v2025.1" format="build">}}/scripts/
```

or

```sh
tar -xf node_agent-{{<yb-version version="v2025.1" format="build">}}-linux-arm64.tar.gz && cd {{<yb-version version="v2025.1" format="build">}}/scripts/
```

### Create data directories or mount points

Configure data directories or mount points for the node (typically `/data`). If you have multiple data drives, these might be for example `/mnt/d0`, `/mnt/d1`, and so on. The data drives must be accessible to the `yugabyte` user that will be created by the script.

### Modify the configuration file

Edit the `node-agent-provision.yaml` file in the scripts directory.

The following table describes options that are changed for a typical installation. The file is commented; you can [review the file](https://github.com/yugabyte/yugabyte-db/blob/{{< yb-version version="v2025.1" format="short">}}/managed/node-agent/resources/node-agent-provision.yaml) and its default settings on GitHub.

| Option | Value |
| :--- | :--- |
| `yb_home_dir` | The directory on the node where YugabyteDB will be installed. |
| `chrony_servers` | The addresses of your Network Time Protocol servers. |
| `yb_user_id` | Provide a UID to be used for the `yugabyte` user. The script creates the `yugabyte` user, and providing the same UID for each node ensures consistency across nodes. |
| `is_airgap` | If you are performing an airgapped installation, set to true. |
| `use_system_level_systemd` | Defaults to false (which uses user-level systemd for service management). |
| `node_ip` | The fully-qualified domain name or IP address of the node you are provisioning. Must be accessible to other nodes. |
| `tmp_directory` | The directory on the node to use for storing temporary files during provisioning. |

Set the following options to have node agent create (or update) the [on-premises provider configuration](../../../configure-yugabyte-platform/on-premises-provider/) where you want to add the node. (YugabyteDB Anywhere must be installed and running.)

| Option | Value |
| :--- | :--- |
| `url` | The base URL of your YugabyteDB Anywhere instance. |
| `customer_uuid` | Your customer ID. To view your customer ID, in YugabyteDB Anywhere, click the **Profile** icon in the top right corner of the window, and choose **User Profile**. |
| `api_key` | Your API token. To obtain this, in YugabyteDB Anywhere, click the Profile icon in the top right corner of the window, and choose **User Profile**. Then click **Generate Key**. |
| `node_name` | A name for the node. |
| `node_external_fqdn` | The fully qualified domain name or IP address of the node, must be accessible from the YugabyteDB Anywhere server. |

Enter the following on-premises provider configuration details. If the provider does not exist, node agent creates it; otherwise, it adds the node instance to the existing provider.

| Option | Value |
| :--- | :--- |
| `provider name` | Name for the provider configuration (if new) or of the existing provider where you want to add the node. |
| `region name` | Name of the region where the node is located. For example, `us-west-1`. |
| `zone name` | Name of the zone where the node is located. For example, `us-west-1a`. |
| `instance_type name` | Name of the instance type to use. If you are creating a new instance type, provide a name to be used internally to identify the type of VM. For example, for cloud provider VMs, you might name the instance type 'c5.large' for AWS, or 'n1-standard-4' for GCP. |
| `cores` | The number of cores. Optional if the node is using an existing instance type. |
| `memory_size` | The amount of memory (in GB) allocated to the instance. Optional if the node is using an existing instance type. |
| `volume_size` | The size of the storage volume (in GB).  Optional if the node is using an existing instance type. |
| `mount_points` | List the mount points for data storage. This is where the data directories are mounted. You need to add the directories before running the script. Optional if the node is using an existing instance type. |

The following options are used for logging the provisioning itself.

| Option | Value |
| :--- | :--- |
| `logging level` | Set the logging level for the node provisioning. |
| `logging directory` | Set the directory where node provisioning log files will be stored. |
| `logging file` | Name of the node provisioning log file. |

### Run the provisioning script

Run the script either as a root user, or via sudo as follows:

```sh
sudo ./node-agent-provision.sh
```

The script provisions the node and installs node agent, and runs preflight checks to ensure the node is ready for provisioning.

If specified, node agent also creates the on-premises provider configuration; or, if the provider configuration already exists, adds the instance to the provider.

After the node is provisioned, reboot the node.

If the preflight check fails, rebooting the node may solve some issues (for example, incorrect ulimit settings).

#### Verify provisioning

After running the script and rebooting the VM, you can verify that provisioning was successful and YugabyteDB Anywhere can communicate with the node by navigating to `https://<yugabytedbanywhere-host-ip>/nodeagent`, where `yugabytedbanywhere-host-ip` is the IP address hosting your YugabyteDB Anywhere instance.

The page lists the node agents that have been activated and their status.

#### Preflight check

For troubleshooting, you can run the script's preflight checks separately as follows:

```sh
sudo ./node-agent-provision.sh --preflight_check
```

## sudo whitelist

If security restrictions require you to explicitly list the commands that you'll be running as root under sudo, you can add the following commands to the sudo whitelist:

```sh
sudo ./node-agent-provision.sh --preflight_check
sudo ./node-agent-provision.sh
```

The underlying fine-grained commands that the script runs during provisioning depend on the version of YugabyteDB Anywhere, and are updated as newer capabilities are incorporated.

To audit the commands that are run by the script, do the following:

1. [Run the preflight check](#preflight-check).

    The preflight check renders templates containing all the bash commands that the script will execute for provisioning.

1. Identify the rendered templates using grep as follows:

    ```sh
    sudo ./node-agent-provision.sh --preflight_check 2>&1 | grep "INFO - /tmp/tmp.*$"
    ```

    You should see output similar to the following:

    ```output
    2025-02-20 23:01:37,290 - commands.provision_command - INFO - /tmp/tmp0ey61a1c

    2025-02-20 23:01:37,290 - commands.provision_command - INFO - /tmp/tmppri1g4r_
    ```

1. Use `cat` or any other CLI tool to inspect the content of these files to understand the code that the script will execute when provisioning a node.

    - The first file in the log is the precheck template.
    - The second file in the log is the actual execution template.

    Note that these files are specific to the operating system and YugabyteDB Anywhere release, and can vary between releases.

## Next steps

If you did not provide details for the provider configuration, you will need to do the following:

1. If the on-premises provider has not been created, create one.

    Refer to [Create the provider configuration](../../../configure-yugabyte-platform/on-premises-provider/).

1. Add the node to the provider.

    Refer to [Add nodes to the on-premises provider](../../../configure-yugabyte-platform/on-premises-nodes/).
