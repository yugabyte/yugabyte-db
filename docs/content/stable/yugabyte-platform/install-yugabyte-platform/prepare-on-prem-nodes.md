---
title: Prepare nodes for on-premises deployment
headerTitle: Prepare nodes for on-premises deployment
linkTitle: Prepare on-prem nodes
description: Prepare YugabyteDB nodes for on-premises deployments.
menu:
  stable_yugabyte-platform:
    identifier: prepare-on-prem-nodes
    parent: install-yugabyte-platform
    weight: 79
type: docs
---

For on-premises deployments of YugabyteDB universes, you need to import nodes that can be managed by YugabyteDB Anywhere.

## Prerequisites

Using YugabyteDB Anywhere (YBA), you can deploy YugabyteDB universes on nodes with the following architectures and operating systems.

### Supported CPU architectures

YBA supports deploying YugabyteDB on both x86 and ARM (aarch64) architecture-based hardware.

Note that support for ARM architectures is unavailable for airgapped setups, because YBA ARM support for [AWS Graviton](https://aws.amazon.com/ec2/graviton/) requires Internet connectivity.

### Supported operating systems

YBA supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/#yugabytedb-anywhere). YBA additionally supports deploying YugabyteDB on Red Hat Enterprise Linux 8. AlmaLinux OS 9 is used by default.

#### Requirements for all OSes

Python 3 is required. If you're using YugabyteDB Anywhere to provision nodes in public clouds, be sure the custom AMI you plan to use has Python 3 installed.

The host AMI must have `gtar` and `zipinfo` installed.

#### Oracle Linux and AlmaLinux notes

YBA support for Oracle Linux 8 and AlmaLinux OS 8 has the following limitations:

* Oracle Linux 8 uses the `firewall-cmd` client to set default target `ACCEPT`.
* On Oracle Linux 8, only the Red Hat Linux-compatible kernel is supported, to allow port changing. The Unbreakable Enterprise Kernel (UEK) is not supported.
* Systemd services are only supported in YugabyteDB Anywhere 2.15.1 and later versions.

## Prepare ports

The following ports must be opened for intra-cluster communication (they do not need to be exposed to your application, only to other nodes in the cluster and the YugabyteDB Anywhere node):

* 7100 - YB-Master RPC
* 9100 - YB-TServer RPC
* 18018 - YB Controller

The following ports must be exposed for intra-cluster communication, and you should expose these ports to administrators or users monitoring the system, as these ports provide diagnostic troubleshooting and metrics:

* 9300 - Prometheus metrics
* 7000 - YB-Master HTTP endpoint
* 9000 - YB-TServer HTTP endpoint
* 11000 - YEDIS API
* 12000 - YCQL API
* 13000 - YSQL API
* 54422 - Custom SSH

The following ports must be exposed for intra-node communication and be available to your application or any user attempting to connect to the YugabyteDB:

* 5433 - YSQL server
* 9042 - YCQL server
* 6379 - YEDIS server

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).

## Prepare nodes

You can prepare nodes for on-premises deployment, as follows:

1. Ensure that the YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](/preview/deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](/preview/deploy/checklist/#public-clouds).
1. Install the prerequisites and verify the system resource limits, as described in [system configuration](/preview/deploy/manual-deployment/system-config).
1. Ensure you have SSH access to the server and root access (or the ability to run `sudo`; the sudo user can require a password but having passwordless access is desirable for simplicity and ease of use).
1. Execute the following command to verify that you can `ssh` into this node (from your local machine if the node has a public address):

    ```sh
    ssh -i your_private_key.pem ssh_user@node_ip
    ```

The following actions are performed with sudo access:

* Create the `yugabyte:yugabyte` user and group.
* Set the home directory to `/home/yugabyte`.
* Create the `prometheus:prometheus` user and group.

  {{< tip title="Tip" >}}
If you are using the LDAP directory for managing system users, you can pre-provision Yugabyte and Prometheus users, as follows:

* Ensure that the `yugabyte` user belongs to the `yugabyte` group.

* Set the home directory for the `yugabyte` user (default `/home/yugabyte`) and ensure that the directory is owned by `yugabyte:yugabyte`. The home directory is used during cloud provider configuration.

* The Prometheus username and the group can be user-defined. You enter the custom user during the cloud provider configuration.
  {{< /tip >}}

* Ensure that you can schedule Cron jobs with Crontab. Cron jobs are used for health monitoring, log file rotation, and cleanup of system core files.

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

* Verify that Python 3 is installed.
* Enable core dumps and set ulimits, as follows:

    ```sh
    *       hard        core        unlimited
    *       soft        core        unlimited
    ```

* Configure SSH, as follows:

  * Disable `sshguard`.
  * Set `UseDNS no` in `/etc/ssh/sshd_config` (disables reverse lookup, which is used for authentication; DNS is still useable).

* Set `vm.swappiness` to 0.
* Set `mount` path permissions to 0755.

{{< note title="Note" >}}
By default, YugabyteDB Anywhere uses OpenSSH for SSH to remote nodes. YugabyteDB Anywhere also supports the use of Tectia SSH that is based on the latest SSH G3 protocol. For more information, see [Enable Tectia SSH](#enable-tectia-ssh).
{{< /note >}}

### Enable Tectia SSH

[Tectia SSH](https://www.ssh.com/products/tectia-ssh/) is used for secure file transfer, secure remote access and tunnelling. YugabyteDB Anywhere is shipped with a trial version of Tectia SSH client that requires a license in order to notify YugabyteDB Anywhere to permanently use Tectia instead of OpenSSH.

To upload the Tectia license, manually copy it at `${storage_path}/yugaware/data/licenses/<license.txt>`, where *storage_path* is the path provided during the Replicated installation.

After the license is uploaded, YugabyteDB Anywhere exposes the runtime flag `yb.security.ssh2_enabled` that you need to enable, as per the following example:

```shell
curl --location --request PUT 'http://<ip>/api/v1/customers/<customer_uuid>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ssh2_enabled'
--header 'Cookie: <Cookie>'
--header 'X-AUTH-TOKEN: <token>'
--header 'Csrf-Token: <csrf-token>'
--header 'Content-Type: text/plain'
--data-raw '"true"'
```
