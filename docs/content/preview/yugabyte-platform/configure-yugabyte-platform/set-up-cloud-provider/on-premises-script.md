---
title: Manually provision on-premises nodes
headerTitle: Manually provision on-premises nodes
linkTitle: Manually provision on-prem nodes
description: Provision the on-premises nodes manually.
headContent: Your SSH user has sudo privileges that require a password
menu:
  preview_yugabyte-platform:
    identifier: on-premises-manual-1
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

Use the following procedure to manually provision nodes for your [on-premises provider configuration](../on-premises/):

- Your [SSH user](../on-premises/#ssh-key-pairs) has sudo privileges that require a password - **Manual setup with script**.
- Your SSH user does not have sudo privileges at all - **Fully manual setup**.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../on-premises-script/" class="nav-link active">
      <i class="fa-regular fa-scroll"></i>
      Manual setup with script
    </a>
  </li>

  <li>
    <a href="../on-premises-manual/" class="nav-link">
      <i class="icon-shell" aria-hidden="true"></i>
      Fully manual setup
    </a>
  </li>
</ul>

If the SSH user configured in the on-premises provider has sudo privileges that require a password, you can provision your nodes by running the pre-provisioning script (`provision_instance.py`).

The script is displayed under **Instances** on the **Instances** tab of the on-prem configuration you created.

{{< warning title="Note" >}}
If the SSH user does not have any sudo privileges at all, you can't use the script and need to perform a [fully manual setup](../on-premises-manual/).
{{< /warning >}}

## Manually provision nodes using the script

You can manually provision each node using the pre-provisioning Python script, as follows:

1. Log in to the YugabyteDB Anywhere virtual machine via SSH.

1. If you installed YugabyteDB Anywhere using Replicated, access the Docker `yugaware` container, as follows:

    ```sh
    sudo docker exec -it yugaware bash
    ```

1. In YugabyteDB Anywhere, navigate to **Configs > Infrastructure > On-Premises Datacenters**, select the on-prem configuration you created, and choose **Instances**.

    ![On-prem pre-provisioning script](/images/yb-platform/config/yba-onprem-config-script.png)

1. Copy and paste the Python script command under **Instances**.

    Set the flags for the command as follows:

    - `--ask_password` - this flag instructs the script to prompt for a password, which is required if the sudo user requires password authentication.
    - `--ip` - enter the IP address of the node.
    - `--mount_points` - enter the mount point configured for the node (typically `/data`). If you have multiple drives, add these as a comma-separated list, such as, for example, `/mnt/d0,/mnt/d1`.
    - `--install_node_agent` - this flag instructs the script to install the node agent, which is required for YugabyteDB Anywhere to communicate with the instance.
    - `--api_token` - enter your API token; you can create an API token by navigating to your **User Profile** and clicking **Generate Key**.
    - `--yba_url` - enter the URL of the machine where you are running YugabyteDB Anywhere, with port 9000. For example, `http://ybahost.company.com:9000`. The node must be able to communicate with YugabyteDB Anywhere at this address.
    - `--node_name` - enter a name for the node.
    - `--instance_type` - enter the name of the [instance type](../on-premises/#add-instance-types) to use for the node. The name must match the name of an existing instance type.
    - `--zone_name` - enter a zone name for the node.

    For example:

    ```bash
    /opt/yugabyte/yugaware/data/provision/9cf26f3b-4c7c-451a-880d-593f2f76efce/provision_instance.py \
        --ask_password \
        --ip 10.9.116.65 \
        --mount_points /data \
        --install_node_agent \
        --api_token 999bc9db-ddfb-9fec-a33d-4f8f9fd88db7 \
        --yba_url http://100.98.0.40:9000 \
        --node_name onprem_node1 \
        --instance_type c5.large \
        --zone_name us-west-2a 
    ```

    Expect the following output and password prompt:

    ```output
    Executing provision now for instance with IP 10.9.116.65...
    SUDO password:
    ```

1. Enter your password.

1. Wait for the script to finish successfully.

1. Repeat steps 4-6 for every node that will participate in the on-prem configuration.

After you have provisioned the nodes, you can proceed to [add instances to the on-prem provider](../on-premises/#add-instances).
