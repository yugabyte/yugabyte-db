---
title: Deploy YugabyteDB
headerTitle: Deploy
linkTitle: 3. Deploy
description: How to start your YugabyteDB database cluster.
menu:
  preview:
    identifier: deploy-1-yugabyted
    parent: deploy-manual-deployment
    weight: 613
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../start-yugabyted/" class="nav-link active">
      yugabyted
    </a>
  </li>
  <li >
    <a href="../start-masters/" class="nav-link">
      Manual
    </a>
  </li>
</ul>

This section describes how to deploy YugabyteDB a single region or data center in a multi-zone/multi-rack configuration using the [yugabyted](../../../reference/configuration/yugabyted/) configuration utility.

Note that single zone configuration is a special case of multi-zone where all placement-related flags are set to the same value across every node.

For instructions on running a single cluster across multiple data centers or 2 clusters in 2 data centers, refer to [Multi-DC deployments](../../../deploy/multi-dc/).

### Deploy a multi-zone cluster

To create a secure multi-zone cluster:

1. Start the first node by running the `yugabyted start` command, using the `--secure` flag and passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<IP_of_VM_1> \
        --backup-daemon=true \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
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

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --backup-daemon=true \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone
    ```

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --backup-daemon=true \
        --cloud_location=aws.us-east-1.us-east-1c \
        --fault_tolerance=zone
    ```
