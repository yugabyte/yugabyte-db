---
title: yugabyted
headerTitle: yugabyted
linkTitle: yugabyted
description: Use yugabyted to deploy YugabyteDB universes.
headcontent: Utility for deploying and managing YugabyteDB
aliases:
  - /stable/deploy/docker/
menu:
  stable:
    identifier: yugabyted
    parent: configuration
    weight: 100
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

Use yugabyted to launch and manage YugabyteDB universes locally on your laptop, or on VMs for production deployments.

{{< youtube id="ah_fPDpZjnc" title="How to Start YugabyteDB on Your Laptop" >}}

{{<note title="Production deployments">}}
You can use yugabyted for production deployments. You can also administer [YB-TServer](../yb-tserver/) and [YB-Master](../yb-master/) servers directly (refer to [Deploy YugabyteDB](../../../deploy/)).
{{</note>}}

## Installation

The yugabyted executable file is packaged with YugabyteDB and located in the YugabyteDB home `bin` directory.

For information on installing YugabyteDB, see [Use a local cluster](/stable/quick-start/linux/).

After installing YugabyteDB, if you want to use [backup](../yugabyted-reference/#backup) and [restore](../yugabyted-reference/#restore), you also need to install the YB Controller service, which manages backup and restore operations. YB Controller is included in the `share` directory of your YugabyteDB installation.

For example, if you installed v{{< yb-version version="stable"  format="short">}}, extract the `ybc-2.2.0.3-b17-linux-x86_64.tar.gz` file into the `ybc` folder as follows:

```sh
cd yugabyte-{{< yb-version version="stable" >}}
mkdir -p ybc && tar -xvf share/ybc-2.2.0.3-b17-linux-x86_64.tar.gz -C ybc --strip-components=1
```

To use the service, when creating nodes run the [yugabyted start](../yugabyted-reference/#start) command with `--backup_daemon=true`:

```sh
./bin/yugabyted start --backup_daemon=true
```

{{% note title="Running on macOS" %}}

Running YugabyteDB on macOS requires additional settings. For more information, refer to [Running on macOS](#running-on-macos).

Note that YB Controller is not supported on macOS.
{{% /note %}}

## Using yugabyted

Run yugabyted commands from your installation `bin` directory.

For a reference to all yugabyted commands, refer to [yugabyted command reference](../yugabyted-reference/)

You can access command-line help for yugabyted by running one of the following from the YugabyteDB home:

```sh
$ ./bin/yugabyted -h
```

```sh
$ ./bin/yugabyted --help
```

For help with specific [yugabyted commands](../yugabyted-reference/), run 'yugabyted [ command ] -h'. For example, you can print the command-line help for the `yugabyted start` command by running the following:

```sh
$ ./bin/yugabyted start -h
```

### Base directory

By default, yugabyted uses the `$HOME/var` directory to store data, configurations, and logs.

You can change the base directory when starting a universe using the `--base_dir` flag.

For example, you can override the base directory when starting the yugabyted node as follows:

```sh
./bin/yugabyted start --base_dir /home/user/node1
```

 If you change the base directory, you _must_ specify the base directory using the `--base-dir` flag when running subsequent commands on the universe. For example, to obtain the status of the universe, you would enter the following:

```sh
./bin/yugabyted status --base_dir  /home/user/node1
```

When simulating running a multi-node universe on your desktop machine (for testing and development, and running examples), you must specify a different base directory for each node (see [Create a local multi-node universe](#create-a-local-multi-node-universe) for an example). When running subsequent commands on local multi-node universes, you must also specify the `--base-dir` flag.

## Create a local cluster

For convenient testing and development, or simply to explore YugabyteDB, you can use yugabyted to create clusters locally on your own machine.

Note that if a local universe is already running, you must [destroy it](#destroy-a-local-universe) before create a new one.

{{<note title="Production deployments">}}
You can use yugabyted for production deployments. You can also administer [YB-TServer](../yb-tserver/) and [YB-Master](../yb-master/) servers directly (refer to [Deploy YugabyteDB](../../../deploy/)).
{{</note>}}

##### Running on macOS

When running local clusters on macOS, keep in mind the following caveats:

- YB Controller (and by extension, backup and restore commands) is not supported on macOS.

- Port conflicts

    macOS Monterey enables AirPlay receiving by default, which listens on port 7000. This conflicts with YugabyteDB and causes `yugabyted start` to fail. Use the [--master_webserver_port flag](../yugabyted-reference/#advanced-flags) when you start the universe to change the default port number, as follows:

    ```sh
    ./bin/yugabyted start --master_webserver_port=9999
    ```

    Alternatively, you can disable AirPlay receiving, then start YugabyteDB normally, and then, optionally, re-enable AirPlay receiving.

- Loopback addresses

    On macOS, every additional node after the first needs a loopback address configured to simulate the use of multiple hosts or nodes. For example, for a three-node universe, you add two additional addresses as follows:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.2
    sudo ifconfig lo0 alias 127.0.0.3
    ```

    The loopback addresses do not persist upon rebooting your computer.

### Create a single-node universe

Create a single-node universe with a given [base directory](#base-directory). You need to provide a fully-qualified directory path for the `base_dir` parameter.

```sh
./bin/yugabyted start --advertise_address=127.0.0.1 \
    --base_dir=/Users/username/yugabyte-{{< yb-version version="stable" >}}/data1
```

Alternatively, you can provide an IPv6 address. For example:

```sh
./bin/yugabyted start --advertise_address=::1 \
    --base_dir=/Users/username/yugabyte-{{< yb-version version="stable" >}}/data1
```

To create secure single-node cluster with [encryption in transit](../../../secure/tls-encryption/) and [authentication](../../../secure/enable-authentication/authentication-ysql/) enabled, add the `--secure` flag as follows:

```sh
# Using IPv4
./bin/yugabyted start --secure --advertise_address=127.0.0.1 \
    --base_dir=/Users/username/yugabyte-{{< yb-version version="stable" >}}/data1
```

When authentication is enabled, the default user is `yugabyte` in YSQL, and `cassandra` in YCQL. When a cluster is started using the `--secure` flag, yugabyted outputs a message `Credentials File is stored at <credentials_file_path.txt>` with the location of the credentials for the default users.

### Create a local multi-node universe

A local multi-node universe provides a quick way to simulate a multi-node YugabyteDB universe on a single computer so that you can explore YugabteDB's distributed capabilities.

To create a universe with multiple nodes, you first create a single node and then create additional nodes using the `--join` flag, providing the address of any already running node to connect them to the universe. If a node is restarted, you would also use the `--join` flag to rejoin the universe.

To create a _secure_ local multi-node universe, ensure you have [generated and copied the certificates](#create-certificates-for-a-secure-local-multi-node-universe) for each node. To create a universe without encryption and authentication, simply omit the `--secure` flag.

To create the universe, do the following:

1. Start the first node by running the following command:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1 \
        --cloud_location=aws.us-east-1.us-east-1a
    ```

1. On macOS, configure loopback addresses for the additional nodes as follows:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.2
    sudo ifconfig lo0 alias 127.0.0.3
    ```

1. Add two more nodes to the universe using the `--join` flag, as follows:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=127.0.0.2 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2 \
        --cloud_location=aws.us-east-1.us-east-1b
    ./bin/yugabyted start --secure --advertise_address=127.0.0.3 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3 \
        --cloud_location=aws.us-east-1.us-east-1c
    ```

When you use the `--secure` flag, yugabyted outputs a message `Credentials File is stored at <credentials_file_path.txt>` with the location of the credentials for the default users.

### Destroy a local universe

If you are running YugabyteDB on your local computer, you can't run more than one universe at a time. To set up a new local YugabyteDB universe using yugabyted, first destroy the currently running universe.

To destroy a local single-node universe, use the [destroy](../yugabyted-reference/#destroy-1) command as follows:

```sh
./bin/yugabyted destroy
```

To destroy a local multi-node universe, use the `destroy` command with the `--base_dir` flag set to the [base directory](#base-directory) path of each of the nodes. For example, for a three node universe, you would execute commands similar to the following:

{{%cluster/cmd op="destroy" nodes="1,2,3"%}}

```sh
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3
```

If the universe has more than three nodes, execute a `destroy --base_dir=<path to directory>` command for each additional node until all nodes are destroyed.

## Manage certificates and authentication

To deploy secure universes using yugabyted, use the `start` command with the `--secure` flag. This enables [encryption in transit](#create-certificates-for-a-secure-local-multi-node-universe) and [authentication](../../../secure/enable-authentication/authentication-ysql/) for the node.

When you start a universe using the `--secure` flag, the credentials for the universe, including password, are output to a credentials file, and the location of the credentials file is displayed on the console.

The default user is `yugabyte` in YSQL, and `cassandra` in YCQL.

To deploy any type of secure universe or use encryption at rest, OpenSSL must be installed on your machine.

### Create certificates for a secure local multi-node universe

Secure universes use [encryption in transit](../../../secure/tls-encryption/), which requires SSL/TLS certificates for each node in the universe.

When starting a secure local single-node universe, a certificate is automatically generated for the universe.

However, to deploy a secure multi-node universe, you must generate the certificates using the `--cert generate_server_certs` command and then copy them to the respective node base directories *before* you create the multi-node universe.

For example, to create the certificates for a local universe, do the following:

```sh
./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1,127.0.0.2,127.0.0.3
```

Certificates are generated in the `<HOME>/var/generated_certs/<hostname>` directory.

Copy the certificates to the respective node's [base directory](#base-directory). For example:

```sh
cp $HOME/var/generated_certs/127.0.0.1/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node1/certs
cp $HOME/var/generated_certs/127.0.0.2/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node2/certs
cp $HOME/var/generated_certs/127.0.0.3/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node3/certs
```

### Enable and disable encryption at rest

To enable [encryption at rest](../../../secure/encryption-at-rest/) in a deployed local universe, run the following command:

```sh
./bin/yugabyted configure encrypt_at_rest \
    --enable \
    --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
```

To enable encryption at rest in a deployed multi-zone or multi-region universe, run the following command from any VM:

```sh
./bin/yugabyted configure encrypt_at_rest --enable
```

To disable encryption at rest in a local universe with encryption at rest enabled, run the following command:

```sh
./bin/yugabyted configure encrypt_at_rest \
    --disable \
    --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
```

To disable encryption at rest in a multi-zone or multi-region universe with this type of encryption enabled, run the following command from any VM:

```sh
./bin/yugabyted configure encrypt_at_rest --disable
```

## Primary clusters

### Create a multi-zone universe

{{< tabpane text=true >}}

  {{% tab header="Secure" lang="secure" %}}

To create a secure multi-zone universe:

1. Start the first node by running the `yugabyted start` command, using the `--secure` flag and passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
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

    For the second node, use the IP address of the first node in the `--join` flag:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-east-1.us-east-1c \
        --fault_tolerance=zone
    ```

yugabyted outputs a message `Credentials File is stored at <credentials_file_path.txt>` with the location of the credentials for the default users.

  {{% /tab %}}

  {{% tab header="Insecure" lang="basic" %}}

To create a multi-zone universe:

1. Start the first node by running the `yugabyted start` command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
    ```

1. Start the second and the third node on two separate VMs using the `--join` flag.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    For the second node, use the IP address of the first node in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-east-1.us-east-1c \
        --fault_tolerance=zone
    ```

  {{% /tab %}}

{{< /tabpane >}}

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the universe as follows:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=zone
```

The preceding command automatically determines the data placement constraint based on the `--cloud_location` of each node in the universe. If there are three or more zones available in the universe, the `configure` command configures the universe to survive at least one availability zone failure. Otherwise, it outputs a warning message.

The replication factor of the universe defaults to 3.

You can set the data placement constraint manually and specify preferred regions using the `--constraint_value` flag, which takes the comma-separated value of `cloud.region.zone:priority`. For example:

```sh
./bin/yugabyted configure data_placement \
    --fault_tolerance=region \
    --constraint_value=aws.us-east-1.us-east-1a:1,aws.us-west-1.us-west-1a,aws.us-central-1.us-central-1a:2
```

This indicates that us-east is the preferred region, with a fallback option to us-central.

You can set the replication factor of the universe manually using the `--rf` flag. For example:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=zone \
    --constraint_value=aws.us-east-1.us-east-1a,aws.us-east-1.us-east-1b,aws.us-east-1.us-east-1c \
    --rf=3
```

### Create a multi-region universe

{{< tabpane text=true >}}

  {{% tab header="Secure" lang="secure-2" %}}

To create a secure multi-region universe:

1. Start the first node by running the `yugabyted start` command, using the `--secure` flag and passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=region
    ```

1. Create certificates for the second and third virtual machine (VM) for SSL and TLS connection, as follows:

    ```sh
    ./bin/yugabyted cert generate_server_certs --hostnames=<IP_of_VM_2>,<IP_of_VM_3>
    ```

1. Manually copy the generated certificates in the first VM to the second and third VM:

    - Copy the certificates for the second VM from `$HOME/var/generated_certs/<IP_of_VM_2>` in the first VM to `$HOME/var/certs` in the second VM.
    - Copy the certificates for third VM from `$HOME/var/generated_certs/<IP_of_VM_3>` in first VM to `$HOME/var/certs` in the third VM.

1. Start the second and the third node on two separate VMs using the `--join` flag.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    For the second node, use the IP address of the first node in the `--join` flag:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-west-1.us-west-1a \
        --fault_tolerance=region
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --secure --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-central-1.us-central-1a \
        --fault_tolerance=region
    ```

yugabyted outputs a message `Credentials File is stored at <credentials_file_path.txt>` with the location of the credentials for the default users.

  {{% /tab %}}

  {{% tab header="Insecure" lang="basic-2" %}}

To create a multi-region universe:

1. Start the first node by running the `yugabyted start` command, pass in the `--cloud_location` and `--fault_tolerance` flags to set the node location details.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=region
    ```

1. Start the second and the third node on two separate VMs using the `--join` flag.

    Set the `--backup_daemon` flag to true if you want to perform backup and restore operations.

    For the second node, use the IP address of the first node in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-west-1.us-west-1a \
        --fault_tolerance=region
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
        --join=<ip-address-first-yugabyted-node> \
        --cloud_location=aws.us-central-1.us-central-1a \
        --fault_tolerance=region
    ```

  {{% /tab %}}

{{< /tabpane >}}

After starting the yugabyted processes on all nodes, configure the data placement constraint of the universe as follows:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=region
```

The preceding command automatically determines the data placement constraint based on the `--cloud_location` of each node in the universe. If there are three or more regions available in the universe, the `configure` command configures the universe to survive at least one availability region failure. Otherwise, it outputs a warning message.

The replication factor of the universe defaults to 3.

You can set the data placement constraint manually and specify preferred regions using the `--constraint_value` flag, which takes the comma-separated value of `cloud.region.zone:priority`. For example:

```sh
./bin/yugabyted configure data_placement \
    --fault_tolerance=region \
    --constraint_value=aws.us-east-1.us-east-1a:1,aws.us-west-1.us-west-1a,aws.us-central-1.us-central-1a:2
```

This indicates that us-east is the preferred region, with a fallback option to us-central.

You can set the replication factor of the universe manually using the `--rf` flag. For example:

```sh
./bin/yugabyted configure data_placement \
    --fault_tolerance=region \
    --constraint_value=aws.us-east-1.us-east-1a,aws.us-west-1.us-west-1a,aws.us-central-1.us-central-1a \
    --rf=3
```

### Create a multi-region universe in Docker

Docker-based deployments are in {{<tags/feature/ea>}}.

You can run yugabyted in a Docker container. For more information, see the [Quick Start](/stable/quick-start/docker/).

The following example shows how to create a multi-region universe. If the `~/yb_docker_data` directory already exists, delete and re-create it.

Note that the `--join` flag only accepts a label that conforms to DNS syntax, so name your Docker container accordingly using only letters, numbers, and hyphens. When joining, you can use the DNS name or IP address of any existing, active node in the universe.

```sh
rm -rf ~/yb_docker_data
mkdir ~/yb_docker_data

docker network create yb-network

docker run -d --name yugabytedb-node1 --hostname yugabytedb-node1 --net yb-network \
    -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
    -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node2 --hostname yugabytedb-node2 --net yb-network \
    -p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
    -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node3 --hostname yugabytedb-node3 --net yb-network \
    -p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
    -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false
```

## Read replicas

To create a read replica cluster, you first need an existing YugabyteDB universe; this example assumes a 3-node universe is deployed. Refer to [Create a local multi-node universe](#create-a-local-multi-node-universe).

Initially universes have only one cluster, called its primary or live cluster.  This cluster consists of all its non-read replica nodes.

To add read replica nodes to the universe, you need to first create a read replica cluster for them to belong to. After you have done that, you add read replica nodes to the universe using the `--join` and `--read_replica` flags. When joining, you can use the DNS name or IP address of any existing, active node in the universe.

### Create a read replica cluster

{{< tabpane text=true >}}

  {{% tab header="Secure" lang="secure-2" %}}

To create a secure read replica cluster, generate and copy the certificates for each read replica node, similar to how you create [certificates for local multi-node universe](#create-certificates-for-a-secure-local-multi-node-universe).

```sh
./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.4,127.0.0.5,127.0.0.6,127.0.0.7,127.0.0.8
```

Copy the certificates to the respective read replica nodes in the `<base_dir>/certs` directory:

```sh
cp $HOME/var/generated_certs/127.0.0.4/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node4/certs
cp $HOME/var/generated_certs/127.0.0.5/* $HOME/yugabyte-{{< yb-version version="stable" >}}/nod45/certs
cp $HOME/var/generated_certs/127.0.0.6/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node6/certs
cp $HOME/var/generated_certs/127.0.0.7/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node7/certs
cp $HOME/var/generated_certs/127.0.0.8/* $HOME/yugabyte-{{< yb-version version="stable" >}}/node8/certs
```

To create the read replica cluster, do the following:

1. On macOS, configure loopback addresses for the additional nodes as follows:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.4
    sudo ifconfig lo0 alias 127.0.0.5
    sudo ifconfig lo0 alias 127.0.0.6
    sudo ifconfig lo0 alias 127.0.0.7
    sudo ifconfig lo0 alias 127.0.0.8
    ```

1. Add read replica nodes using the `--join` and `--read_replica` flags, as follows:

    ```sh
    ./bin/yugabyted start \
        --secure \
        --advertise_address=127.0.0.4 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node4 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=127.0.0.5 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node5 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=127.0.0.6 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node6 \
        --cloud_location=aws.us-east-1.us-east-1e \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=127.0.0.7 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node7 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=127.0.0.8 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node8 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica
    ```

  {{% /tab %}}

  {{% tab header="Insecure" lang="basic-2" %}}

To create the read replica cluster, do the following:

1. On macOS, configure loopback addresses for the additional nodes as follows:

    ```sh
    sudo ifconfig lo0 alias 127.0.0.4
    sudo ifconfig lo0 alias 127.0.0.5
    sudo ifconfig lo0 alias 127.0.0.6
    sudo ifconfig lo0 alias 127.0.0.7
    sudo ifconfig lo0 alias 127.0.0.8
    ```

1. Add read replica nodes using the `--join` and `--read_replica` flags, as follows:

    ```sh
    ./bin/yugabyted start \
        --advertise_address=127.0.0.4 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node4 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --advertise_address=127.0.0.5 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node5 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --advertise_address=127.0.0.6 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node6 \
        --cloud_location=aws.us-east-1.us-east-1e \
        --read_replica

    ./bin/yugabyted start \
        --advertise_address=127.0.0.7 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node7 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica

    ./bin/yugabyted start \
        --advertise_address=127.0.0.8 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node8 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica
    ```

  {{% /tab %}}

{{< /tabpane >}}

### Configure a new read replica cluster

After starting all read replica nodes, configure the read replica cluster using `configure_read_replica new` command as follows:

```sh
./bin/yugabyted configure_read_replica new --base_dir ~/yb-cluster/node4
```

The preceding command automatically determines the data placement constraint based on the `--cloud_location` of each node in the universe. After the command is run, the primary cluster will begin asynchronous replication with the read replica cluster.

You can set the data placement constraint manually and specify the number of replicas in each cloud location using the `--data_placement_constraint` flag, which takes the comma-separated value of `cloud.region.zone:num_of_replicas`. For example:

```sh
./bin/yugabyted configure_read_replica new \
    --base_dir ~/yb-cluster/node4 \
    --constraint_value=aws.us-east-1.us-east-1d:1,aws.us-east-1.us-east-1e:1,aws.us-east-1.us-east-1d:1
```

When specifying the `--data_placement_constraint` flag, you must provide the following:

- include all the zones where a read replica node is to be placed.
- specify the number of replicas for each zone; each zone should have at least one read replica node.

    The number of replicas in any cloud location should be less than or equal to the number of read replica nodes deployed in that cloud location.

The replication factor of the read replica cluster defaults to the number of different cloud locations containing read replica nodes; that is, one replica in each cloud location.

You can set the replication factor manually using the `--rf` flag. For example:

```sh
./bin/yugabyted configure_read_replica new \
    --base_dir ~/yb-cluster/node4 \
    --rf <replication_factor>
```

When specifying the `--rf` flag:

- If the `--data_placement_constraint` flag is provided
  - All rules for using the `--data_placement_constraint` flag apply.
  - Replication factor should be equal the number of replicas specified using the `--data_placement_constraint` flag.
- If the `--data_placement_constraint` flag is not provided:
  - Replication factor should be less than or equal to total read replica nodes deployed.
  - Replication factor should be greater than or equal to number of cloud locations that have a read replica node; that is, there should be at least one replica in each cloud location.

### Modify a configured read replica cluster

You can modify an existing read replica cluster configuration using the `configure_read_replica modify` command and specifying new values for the `--data_placement_constraint` and `--rf` flags.

For example:

```sh
./yugabyted configure_read_replica modify \
--base_dir=~/yb-cluster/node4 \
--data_placement_constraint=aws.us-east-1.us-east-1d:2,aws.us-east-1.us-east-1e:1,aws.us-east-1.us-east-1d:2
```

This changes the data placement configuration of the read replica cluster to have 2 replicas in `aws.us-east-1.us-east-1d` cloud location as compared to one replica set in the original configuration.

When specifying new `--data_placement_constraint` or `--rf` values, the same rules apply.

### Delete a read replica cluster

To delete a read replica cluster, destroy all read replica nodes using the `destroy` command:

```sh
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node4
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node5
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node6
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node7
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node8
```

After destroying the nodes, run the `configure_read_replica delete` command to delete the read replica configuration:

```sh
./bin/yugabyted configure_read_replica delete --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
```

## xCluster

Follow the instructions in [xCluster setup](../../../deploy/multi-dc/async-replication/async-transactional-setup-automatic/).

## Upgrade a universe

{{< warning title="Upgrading to PostgreSQL 15" >}}
For information on upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2.25.1 or later), refer to [YSQL major upgrade](../../../manage/ysql-major-upgrade-yugabyted/).

Upgrading to PostgreSQL 15 versions is only supported from v2024.2.2 or later.
{{< /warning >}}

To use the latest features of the database and apply the latest security fixes, upgrade your YugabyteDB universe to the [latest release](https://download.yugabyte.com/#/).

Upgrading an existing YugabyteDB universe that was deployed using yugabyted includes the following steps:

1. Verify the version compatibility before upgrading the universe.

    ```sh
    ./bin/yugabyted upgrade check_version_compatibility
    ```

1. Stop one of the running nodes using the `yugabyted stop` command with the `--upgrade` flag.

    ```sh
    ./bin/yugabyted stop --upgrade true --base_dir <path_to_base_dir>
    ```

1. Wait for 60 seconds.

1. Start the new yugabyted process (from the new downloaded release) by executing the `yugabyted start` command. Use the previously configured `--base_dir` when restarting the instance.

    ```sh
    ./bin/yugabyted start --base_dir <path_to_base_dir>
    ```

1. Repeat steps 2-4 for all the nodes. Wait 60 seconds before repeating the steps on each node.

1. After restarting all the nodes, upgrade the [YSQL catalog](../../../architecture/system-catalog/) of the universe. This command can be run from any node.

    ```sh
    ./bin/yugabyted upgrade ysql_catalog --base_dir <path_to_base_dir>
    ```

1. After successful YSQL catalog upgrade, restart all the nodes for a second time.

    Repeat steps 2-3 for all the nodes. Wait 60 seconds before repeating the steps on each node.

1. After restarting all the nodes, finalize the upgrade by running the `yugabyted finalize_new_version` command. This command can be run from any node.

    ```sh
    ./bin/yugabyted upgrade finalize_new_version --base_dir <path_to_base_dir>
    ```

    Use the `timeout` flag to specify a custom timeout for the operation. Default value is 60000 ms.

    ```sh
    ./bin/yugabyted upgrade finalize_new_version --base_dir <path_to_base_dir> --timeout 80000
    ```

## Scale a universe from single to multi zone

The following steps assume that you have a running YugabyteDB universe deployed using yugabyted:

1. Stop the first node by using `yugabyted stop` command:

    ```sh
    ./bin/yugabyted stop
    ```

1. Start the YugabyteDB node by using `yugabyted start` command by providing the necessary cloud information as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> \
      --cloud_location=aws.us-east-1.us-east-1a \
      --fault_tolerance=zone
    ```

1. Repeat the previous step on all the nodes of the universe, one node at a time. If you are deploying the universe on your local computer, specify the [base directory](#base-directory) for each node using the `--base-dir` flag.

1. After starting all nodes, specify the data placement constraint on the universe using the following command:

    ```sh
    ./bin/yugabyted configure data_placement --fault_tolerance=zone
    ```

    To manually specify the data placement constraint, use the following command:

    ```sh
    ./bin/yugabyted configure data_placement \
      --fault_tolerance=zone \
      --constraint_value=aws.us-east-1.us-east-1a,aws.us-east-1.us-east-1b,aws.us-east-1.us-east-1c \
      --rf=3
    ```
