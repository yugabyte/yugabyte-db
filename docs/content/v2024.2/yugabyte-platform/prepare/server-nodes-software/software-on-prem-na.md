---
title: YugabyteDB Anywhere on-premises node provisioning
headerTitle: Provision on-premises nodes
linkTitle: Provision nodes
description: Software requirements for on-premises provider nodes.
headContent: How to meet the software prerequisites for database nodes
menu:
  v2024.2_yugabyte-platform:
    identifier: software-on-prem-1-na
    parent: software-on-prem
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../software-on-prem-na/" class="nav-link active">
      <i class="fa-solid fa-user-secret"></i>Node Agent</a>
  </li>

  <li>
    <a href="../software-on-prem-auto/" class="nav-link">
      <i class="fa-regular fa-briefcase" aria-hidden="true"></i>Classic</a>
  </li>

</ul>

The [YugabyteDB Anywhere Node agent](/preview/faq/yugabyte-platform/#what-is-a-node-agent) is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the nodes in universes. Node agent can also be used to provision on-premises nodes.

After configuring setup options and running the script, the script provisions the node and installs the node agent. Optionally, if YugabyteDB Anywhere is installed and is running, node agent also creates or updates the on-premises provider that the node is to be part of.

## Download node agent provisioner

To begin, download the node agent provisioning package to the node you want to provision.

- If YugabyteDB Anywhere has been installed and is running, you can download the node agent package from YugabyteDB Anywhere using the following API command:

    ```sh
    curl https://<yba_address>/api/v1/node_agents/download\?downloadType\=package\&os\=LINUX\&arch\=AMD64 --fail --header 'X-AUTH-YW-API-TOKEN: <api_token>'  > node-agent.tar.gz
    ```

    `<yba_address>` is the address of your YugabyteDB Anywhere installation. `<api_token>` is an API token you created. For information on creating an API token, refer to [API authentication](../../../anywhere-automation/#authentication).

- You can also download the latest package at the following address:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{<yb-version version="preview" format="long">}}/yba_installer_full-{{<yb-version version="preview" format="build">}}-node-agent.tar.gz
    ```

Extract the package and go to the `scripts` directory.

```sh
tar -xvzf node-agent.tar.gz && cd <to-do>/scripts/
```

## Provision the node

To provision the node:

1. Edit the `node-agent-provision.yaml` file in the scripts directory.

    Set the following options in the provisioning file to the correct values:

    | Option | Value |
    | :--- | :--- |
    | `yb_home_dir` | The directory on the node where YugabyteDB will be installed. |
    | `chrony_servers` | The addresses of your Network Time Protocol servers. |
    | `yb_user_id` | The UID for the `yugabyte` user. |
    | `is_airgap` | If you are performing an airgapped installation, set to true. |
    | `use_system_level_systemd` | Set to true to use system-level systemd for service management. YugabyteDB Anywhere will require SSH access to the node. |
    | `node_ip` | The IP address of the node you are provisioning. Must be accessible to other nodes. |
    | `tmp_directory` | The directory on the node to use for storing temporary files during provisioning. |

1. Optionally, if YugabyteDB Anywhere is running, you can set the following options to have node agent create (or update) the [on-premises provider configuration](../../../configure-yugabyte-platform/on-premises-provider/) where you want to add the node.

    | Option | Value |
    | :--- | :--- |
    | `url` | The base URL of your YugabyteDB Anywhere instance. |
    | `customer_uuid` | Your customer ID. To view your customer ID, in YugabyteDB Anywhere, click the **Profile** icon in the top right corner of the window, and choose **User Profile**. |
    | `api_key` | You API token. |
    | `node_name` | A name for the node. |
    | `node_external_fqdn` | The external FQDN or IP address of the node. Must be accessible from YugabyteDB Anywhere. |

    Enter the following provider details. If the provider does not exist, node agent creates it; otherwise, it adds the node instance to the existing provider.

    | Option | Value |
    | :--- | :--- |
    | `provider name` | Name for the provider (if new) or of the existing provider where you want to add the node. |
    | `region name` | Name of the region where the node is located. For example, `us-west-1`. |
    | `zone name` | Name of the zone where the node is located. For example, `us-west-1a`. |
    | `instance_type name` | Name of the instance type to use. If you are creating a new instance type, provide a name to be used internally to identify the type of VM. For example, for cloud provider VMs, you might name the instance type 'c5.large' for AWS, or 'n1-standard-4' for GCP. |
    | `cores` | The number of cores. Optional if the node is using an existing instance type. |
    | `memory_size` | The amount of memory (in GB) allocated to the instance. Optional if the node is using an existing instance type. |
    | `volume_size` | The size of the storage volume (in GB).  Optional if the node is using an existing instance type. |
    | `mount_points` | List the mount points for data storage. This is where the data directories are mounted. Optional if the node is using an existing instance type. |

    The following options are used for logging the provisioning itself.

    | Option | Value |
    | :--- | :--- |
    | `logging level` | Set the logging level for the node provisioning. |
    | `logging directory` | Set the directory where node provisioning log files will be stored. |
    | `logging file` | Name of the node provisioning log file. |

1. Run the script.

    ```sh
    sudo ./node-agent-provision.sh
    ```

The script provisions the node and installs node agent.

If specified, node agent creates the on-premises provider configuration; or, if the provider already exists, adds the instance to the provider.

## Next steps

If you did not provide configuration details for the provider, you will need to do the following:

1. If the on-premises provider has not been created, create one.

    Refer to [Create the provider configuration](../../../configure-yugabyte-platform/on-premises-provider/).

1. Add the node to the provider.

    Refer to [Add nodes to the on-premises provider](../../../configure-yugabyte-platform/on-premises-nodes/).
