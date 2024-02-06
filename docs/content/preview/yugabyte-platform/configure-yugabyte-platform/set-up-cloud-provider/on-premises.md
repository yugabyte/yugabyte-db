---
title: Configure an on-premises provider configuration
headerTitle: Create on-premises provider configuration
linkTitle: On-premises
description: Configure the on-premises provider configuration.
headContent: For deploying universes on your private cloud
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/onprem
menu:
  preview_yugabyte-platform:
    identifier: set-up-on-premises
    parent: set-up-cloud-provider
    weight: 20
type: docs
---

Before you can deploy universes to private clouds using YugabyteDB Anywhere (YBA), you must create a provider configuration.

With on-premises providers, VMs are _not_ auto-created by YugabyteDB Anywhere; you must manually create your VMs, provision them with YugabyteDB software, and then add them to the provider's free pool of nodes.

YBA supports 3 ways of preparing nodes for running YugabyteDB depending upon the level of access provided to YBA:

| Provisioning | Description |
| :--- | :--- |
| Automatic | YBA is provided an SSH user with sudo access for the nodes it needs to provision. For example, the `ec2-user` for AWS EC2 instances. |
| Assisted&nbsp;manual | YBA doesn't have access to an SSH user with sudo access, but the user can run a script interactively on YBA and provide the script with parameters for credentials for the SSH user with sudo access. |
| Fully manual | Neither YBA nor the user has access to an SSH user with sudo access; only a local (non-SSH) user is available with sudo access. In this case the user has to follow manual steps (link to fully manual provisioning) to provision the node. |

## Overview

To create, provision, and add nodes to your on-premises provider, you will perform tasks in roughly three stages.

### Stage 1: Prepare your infrastructure

- Have your network administrator set up firewalls to open the ports required for YBA and the nodes to communicate. Refer to [Prepare your network](../../../install-yugabyte-platform/prepare-on-prem-nodes/).
- Have your system administrator create VMs that will be used as nodes in universes. This is typically done using your hypervisor or cloud provider. Do the following:
  - Locate the VMs in the regions and availability zones where you will be deploying universes.
  - Install a YugabyteDB-supported Linux OS on the VMs.
  - Set up a `yugabyte` user with root privileges (SSH access and sudo-capable).

  For guidelines on creating VMs that are suitable for deploying YugabyteDB, refer to [Prepare VMs](#prepare-vms).

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
1. Add the VMs (instances). How you do this depends on whether you are using automatic or manual provisioning.

    - Automatic - YBA automatically provisions the VMs that you add and you don't have to take any action.
    - Assisted manual - you run a script provided by YBA to provision each VM before adding the VM to the pool.
    - Fully manual - you follow a sequence of steps to provision each VM manually before adding the VM to the pool.

1. Run pre-checks to validate the nodes you added.

Refer to [Add nodes to the on-premises provider](../on-premises-nodes/).

## Prepare VMs

You can prepare VMs for use as nodes in an on-premises deployment, as follows:

1. Ensure that the nodes conform to the requirements outlined in the [YugabyteDB deployment checklist](../../../../deploy/checklist/).

    This checklist also gives an idea of [recommended instance types across public clouds](../../../../deploy/checklist/#public-clouds).

1. Install the prerequisites and verify the system resource limits, as described in [system configuration](../../../../deploy/manual-deployment/system-config).
1. Ensure you have SSH access to the server and root access (or the ability to run `sudo`; the sudo user can require a password but having passwordless access is desirable for simplicity and ease of use).
1. Execute the following command to verify that you can `ssh` into the node (from your local machine if the node has a public address):

    ```sh
    ssh -i your_private_key.pem ssh_user@node_ip
    ```

The following actions are performed with sudo access:

- Create the `yugabyte:yugabyte` user and group.
- Set the home directory to `/home/yugabyte`.
- Create the `prometheus:prometheus` user and group.

  {{< tip title="Tip" >}}
If you are using the LDAP directory for managing system users, you can pre-provision Yugabyte and Prometheus users, as follows:

- Ensure that the `yugabyte` user belongs to the `yugabyte` group.

- Set the home directory for the `yugabyte` user (default `/home/yugabyte`) and ensure that the directory is owned by `yugabyte:yugabyte`. The home directory is used during cloud provider configuration.

- The Prometheus username and the group can be user-defined. You enter the custom user during the cloud provider configuration.
  {{< /tip >}}

- Ensure that you can schedule Cron jobs with Crontab. Cron jobs are used for health monitoring, log file rotation, and cleanup of system core files.

  {{< tip title="Tip" >}}
For any third-party Cron scheduling tools, you can disable Crontab and add the following Cron entries:

```sh
# Ansible: cleanup core files hourly
0 * * * * /home/yugabyte/bin/clean_cores.sh
# Ansible: cleanup yb log files hourly
5 * * * * /home/yugabyte/bin/zip_purge_yb_logs.sh
# Ansible: Check liveness of master
*/1 * * * * /home/yugabyte/bin/yb-server-ctl.sh master cron-check || /home/yugabyte/bin/yb-server-ctl.sh master start
# Ansible: Check liveness of tserver
*/1 * * * * /home/yugabyte/bin/yb-server-ctl.sh tserver cron-check || /home/yugabyte/bin/yb-server-ctl.sh tserver start
```

Disabling Crontab creates alerts after the universe is created, but they can be ignored. You need to ensure Cron jobs are set appropriately for YugabyteDB Anywhere to function as expected.
  {{< /tip >}}

- Verify that Python 3 is installed.
- Enable core dumps and set ulimits, as follows:

    ```sh
    *       hard        core        unlimited
    *       soft        core        unlimited
    ```

- Configure SSH, as follows:

  - Disable `sshguard`.
  - Set `UseDNS no` in `/etc/ssh/sshd_config` (disables reverse lookup, which is used for authentication; DNS is still useable).

- Set `vm.swappiness` to 0.
- Set `mount` path permissions to 0755.

{{< note title="Note" >}}
By default, YugabyteDB Anywhere uses OpenSSH for SSH to remote nodes. YugabyteDB Anywhere also supports the use of Tectia SSH that is based on the latest SSH G3 protocol. For more information, see [Enable Tectia SSH](#enable-tectia-ssh).
{{< /note >}}

### Enable Tectia SSH

[Tectia SSH](https://www.ssh.com/products/tectia-ssh/) is used for secure file transfer, secure remote access and tunnelling. YugabyteDB Anywhere is shipped with a trial version of Tectia SSH client that requires a license in order to notify YugabyteDB Anywhere to permanently use Tectia instead of OpenSSH.

To upload the Tectia license, manually copy it at `${storage_path}/yugaware/data/licenses/<license.txt>`, where _storage_path_ is the path provided during the Replicated installation.

After the license is uploaded, YugabyteDB Anywhere exposes the runtime flag `yb.security.ssh2_enabled` that you need to enable, as per the following example:

```shell
curl --location --request PUT 'http://<ip>/api/v1/customers/<customer_uuid>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ssh2_enabled'
--header 'Cookie: <Cookie>'
--header 'X-AUTH-TOKEN: <token>'
--header 'Csrf-Token: <csrf-token>'
--header 'Content-Type: text/plain'
--data-raw '"true"'
```

<!--
Creating an on-premises provider requires the following steps:

1. Create your VMs. Do this using your hypervisor or cloud provider. You will need the IP addresses of the VMs.
1. [Create the on-premises provider configuration](#create-a-provider). The provider configuration includes details such as the SSH user you will use to access your VMs while setting up the provider, and the regions where the nodes are located.
1. Specify the compute [instance types](#add-instance-types) that will be used in this provider.
1. If required, [manually provision the VMs](../on-premises-script/) with the YugabyteDB database software. If your SSH user has password-less sudo access to the nodes, you can skip this step, as YBA will be able to automatically provision the nodes.
1. [Add the compute instances](#add-instances) that the provider will use for deploying YugabyteDB universes to the pool of nodes.

![Configure on-prem provider](/images/yb-platform/config/yba-onprem-config-flow.png)
-->
