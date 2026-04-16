---
title: Deploy YugabyteDB
headerTitle: 3. Deploy
linkTitle: 3. Deploy
description: How to start your YugabyteDB database cluster.
menu:
  v2.25:
    identifier: deploy-1-yugabyted
    parent: deploy-manual-deployment
    weight: 613
type: docs
---

This section describes how to deploy YugabyteDB in a single region or data center in a multi-zone/multi-rack configuration.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../start-yugabyted/" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li >
    <a href="../start-masters/" class="nav-link">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>

Use the [yugabyted](../../../reference/configuration/yugabyted/) configuration utility to deploy and manage clusters.

The yugabyted executable file is packaged with YugabyteDB and located in the YugabyteDB home bin directory.

## Optional settings

Depending on your specific deployment, consider setting the following additional flags.

### YCQL only deployment

If you are only using the YCQL API, you must turn off [YSQL memory optimization](../../../reference/configuration/yb-tserver/#memory-division-flags) by adding the following to `--tserver_flags`:

```sh
--tserver_flags "use_memory_defaults_optimized_for_ysql=false"
```

### YSQL Connection Manager

If you want to use [YSQL Connection Manager](../../../additional-features/connection-manager-ysql/) for connection pooling, add the following to `--tserver_flags`:

```sh
--tserver_flags "enable_ysql_conn_mgr=true"
```

### YB Controller service

After installing YugabyteDB, if you want to use backup and restore, you also need to install the YB Controller service, which manages backup and restore operations. YB Controller is included in the `share` directory of your YugabyteDB installation.

For example, if you installed v{{< yb-version version="stable">}}, extract the `ybc-2.0.0.0-b19-linux-x86_64.tar.gz` file into the `ybc` folder as follows:

```sh
cd yugabyte-{{< yb-version version="stable" >}}
mkdir ybc | tar -xvf share/ybc-2.0.0.0-b19-linux-x86_64.tar.gz -C ybc --strip-components=1
```

## Deploy a multi-zone cluster

Note that single zone configuration is a special case of multi-zone where all placement-related flags are set to the same value across every node.

For instructions on running a single cluster across multiple data centers or 2 clusters in 2 data centers, refer to [Multi-DC deployments](../../../deploy/multi-dc/).

To create a secure multi-zone cluster:

1. Start the first node by running the `yugabyted start` command, using the `--secure` flag and passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    Add flags to `--tserver_flags` as required.

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<IP_of_VM_1> \
        --backup_daemon=true \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone \
        --tserver_flags="enable_ysql_conn_mgr=true"
    ```

1. Create certificates for the second and third virtual machine (VM) for SSL and TLS connection, as follows:

    ```sh
    ./bin/yugabyted cert generate_server_certs --hostnames=<IP_of_VM_2>,<IP_of_VM_3>
    ```

1. Manually copy the generated certificates in the first VM to the second and third VM, as follows:

    - Copy the certificates for the second VM from `$HOME/var/generated_certs/<IP_of_VM_2>` in the first VM to `$HOME/var/certs` in the second VM.

    - Copy the certificates for the third VM from `$HOME/var/generated_certs/<IP_of_VM_3>` in first VM to `$HOME/var/certs` in the third VM.

1. Start the second and the third node on two separate VMs using the `--join` flag.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    For the second node, use the IP address of the first node in the `--join` flag:

    Add flags to `--tserver_flags` as required.

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<IP_of_VM_2> \
        --join=<ip-address-first-yugabyted-node> \
        --backup_daemon=true \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone \
        --tserver_flags="enable_ysql_conn_mgr=true"
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<IP_of_VM_3> \
        --join=<ip-address-first-yugabyted-node> \
        --backup_daemon=true \
        --cloud_location=aws.us-east-1.us-east-1c \
        --fault_tolerance=zone \
        --tserver_flags="enable_ysql_conn_mgr=true"
    ```

The credentials for the universe, including password, are output to a credentials file, and the location of the credentials file is output to the console.

For more information on the yugabyted start command, refer to [start](../../../reference/configuration/yugabyted/#start).

## Grow the cluster

To grow the cluster, add additional nodes just as you do when creating the cluster.
