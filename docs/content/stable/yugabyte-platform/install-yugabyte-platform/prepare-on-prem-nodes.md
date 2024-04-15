---
title: Prepare nodes for on-premises deployment
headerTitle: Prepare nodes for on-premises deployment
linkTitle: Prepare nodes
description: Prepare YugabyteDB nodes for on-premises deployments.
headContent: Port settings and VM configuration
menu:
  stable_yugabyte-platform:
    identifier: prepare-on-prem-nodes
    parent: install-yugabyte-platform
    weight: 79
type: docs
---

YugabyteDB Anywhere needs to be able to access nodes that will be used to create universes, and the nodes that make up universes need to be accessible to each other and to applications.

## Prepare ports

The following ports must be opened for intra-cluster communication (they do not need to be exposed to your application, only to other nodes in the cluster and the YugabyteDB Anywhere node):

- 7100 - YB-Master RPC
- 9100 - YB-TServer RPC
- 18018 - YB Controller

The following ports must be exposed for intra-cluster communication. You should expose these ports to administrators or users monitoring the system, as these ports provide diagnostic troubleshooting and metrics:

- 9300 - Prometheus Node Exporter
- 7000 - YB-Master HTTP endpoint
- 9000 - YB-TServer HTTP endpoint
- 11000 - YEDIS API
- 12000 - YCQL API
- 13000 - YSQL API
- 54422 - Custom SSH

The following ports must be exposed for intra-node communication and be available to your application or any user attempting to connect to the YugabyteDB universes:

- 5433 - YSQL server
- 9042 - YCQL server
- 6379 - YEDIS server

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).

## Prepare VMs

You can prepare VMs for use as nodes in an on-premises deployment, as follows:

1. Ensure that the nodes conform to the requirements outlined in the [YugabyteDB deployment checklist](../../../deploy/checklist/).

    This checklist also gives an idea of [recommended instance types across public clouds](../../../deploy/checklist/#public-clouds).

1. Install the prerequisites and verify the system resource limits, as described in [system configuration](../../../deploy/manual-deployment/system-config).
1. Ensure you have SSH access to the server and root access (or the ability to run `sudo`; the sudo user can require a password but having passwordless access is desirable for simplicity and ease of use). If your SSH user does not have sudo privileges at all, follow the steps in [Manually provision on-premises nodes](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises-manual/).
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

Disabling Crontab creates alerts after the universe is created, but they can be ignored. You need to ensure Cron jobs are set appropriately for YBA to function as expected.
  {{< /tip >}}

- Verify that Python 3.5-3.8 is installed. v3.6 is recommended.

    In case there is more than one Python 3 version installed, ensure that `python3` refers to the right one. For example:

    ```sh
    sudo alternatives --set python3 /usr/bin/python3.6
    sudo alternatives --display python3
    python3 -V
    ```

    If you are using Python later than v3.6, install the [selinux](https://pypi.org/project/selinux/) package corresponding to your version of python. For example, using [pip](https://pip.pypa.io/en/stable/installation/), you can install as follows:

    ```sh
    python3 -m pip install selinux
    ```

    Refer to [Ansible playbook fails with libselinux-python aren't installed on RHEL8](https://access.redhat.com/solutions/5674911) for more information.

    If you are using Python later than v3.7, set the **Max Python Version (exclusive)** Global Configuration option to the python version. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

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
By default, YBA uses OpenSSH for SSH to remote nodes. YBA also supports the use of Tectia SSH that is based on the latest SSH G3 protocol. For more information, see [Enable Tectia SSH](#enable-tectia-ssh).
{{< /note >}}

### Enable Tectia SSH

[Tectia SSH](https://www.ssh.com/products/tectia-ssh/) is used for secure file transfer, secure remote access and tunnelling. YBA is shipped with a trial version of Tectia SSH client that requires a license in order to notify YBA to permanently use Tectia instead of OpenSSH.

To upload the Tectia license, manually copy it at `${storage_path}/yugaware/data/licenses/<license.txt>`, where _storage_path_ is the path provided during the Replicated installation.

After the license is uploaded, YBA exposes the runtime flag `yb.security.ssh2_enabled` that you need to enable, as per the following example:

```shell
curl --location --request PUT 'http://<ip>/api/v1/customers/<customer_uuid>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ssh2_enabled'
--header 'Cookie: <Cookie>'
--header 'X-AUTH-TOKEN: <token>'
--header 'Csrf-Token: <csrf-token>'
--header 'Content-Type: text/plain'
--data-raw '"true"'
```
