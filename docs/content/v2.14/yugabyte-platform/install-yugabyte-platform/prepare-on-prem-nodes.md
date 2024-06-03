---
title: Prepare nodes for on-premises deployment
headerTitle: Prepare nodes for on-premises deployment
linkTitle: Prepare nodes
description: Prepare YugabyteDB nodes for on-premises deployments.
menu:
  v2.14_yugabyte-platform:
    identifier: prepare-on-prem-nodes
    parent: install-yugabyte-platform
    weight: 79
type: docs
---

For on-premises deployments of YugabyteDB universes, you need to import nodes that can be managed by YugabyteDB Anywhere.

## Ports

The following ports must be opened for intra-cluster communication (they do not need to be exposed to your application, only to other nodes in the cluster and the YugabyteDB Anywhere node):

* 7100 - Master RPC
* 9100 - TServer RPC

The following ports must be exposed for intra-cluster communication, and you should expose these ports to administrators or users monitoring the system, as these ports provide diagnostic troubleshooting and metrics:

* 9300 - Prometheus metrics
* 7000 - Master HTTP endpoint
* 9000 - TServer HTTP endpoint
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

You can prepare nodes for on premises deployment, as follows:

1. Ensure that the YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](/preview/deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](/preview/deploy/checklist/#running-on-public-clouds).
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
If you are using the LDAP directory for managing system users, you can preprovision Yugabyte and Prometheus users, as follows:

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
