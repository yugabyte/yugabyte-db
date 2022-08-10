---
title: yugabyted reference
headerTitle: yugabyted
linkTitle: yugabyted
description: Use yugabyted to run single-node YugabyteDB clusters.
menu:
  preview:
    identifier: yugabyted
    parent: configuration
    weight: 2451
type: docs
rightNav:
  hideH4: true
---

YugabyteDB uses a 2-server architecture with YB-TServers managing the data and YB-Masters managing the metadata. However, this can introduce a burden on new users who want to get started right away. To manage YugabyteDB for testing and learning purposes, you can use `yugabyted`, which is a database server that acts as a parent server across the [`yb-tserver`](../yb-tserver/) and [`yb-master`](../yb-master/) servers. yugabyted also provides a UI similar to the YugabyteDB Anywhere UI, with a data placement map and metrics dashboard.

The `yugabyted` executable file is located in the YugabyteDB home's `bin` directory.

Using yugabyted, you can create single-node clusters, and, using the `--join` flag in the `start` command, multi-node clusters.

Note that yugabyted is not recommended for production deployments. For production deployments with fully-distributed multi-node clusters, use [`yb-tserver`](../yb-tserver/) and [`yb-master`](../yb-master/) directly. Refer to [Deploy YugabyteDB](../../../deploy).

## Syntax

```sh
yugabyted [-h] [ <command> ] [ <flags> ]
```

- *command*: command to run
- *flags*: one or more flags, separated by spaces.

### Example

```sh
$ ./bin/yugabyted start
```

### Online help

You can access the overview command line help for `yugabyted` by running one of the following examples from the YugabyteDB home.

```sh
$ ./bin/yugabyted -h
```

```sh
$ ./bin/yugabyted -help
```

For help with specific `yugabyted` commands, run 'yugabyted [ command ] -h'. For example, you can print the command line help for the `yugabyted start` command by running the following:

```sh
$ ./bin/yugabyted start -h
```

## Commands

The following commands are available:

- [start](#start)
- [configure](#configure)
- [stop](#stop)
- [destroy](#destroy)
- [status](#status)
- [version](#version)
- [collect_logs](#collect-logs)
- [connect](#connect)
- [demo](#demo)

-----

### start

Use the `yugabyted start` command to start a one-node YugabyteDB cluster in your local environment. This allows you to quickly get started with a YugabyteDB cluster for running [YSQL](../../../architecture/layered-architecture/#yugabyte-sql-ysql) and [YCQL](../../../architecture/layered-architecture/#yugabyte-cloud-ql-ycql) workloads.

#### Syntax

```text
Usage: yugabyted start [flags]
```

Examples:

- Create a local single-node cluster:

  ```sh
  yugabyted start
  ```

- Create a single-node locally and join other nodes that are part of the same cluster:

  ```sh
  yugabyted start --join=host:port,[host:port]
  ```

#### Flags

-h, --help
: Print the command line help and exit.

--advertise_address *bind-ip*
: IP address or local hostname on which yugabyted will listen.

--join *master-ip*
: The IP address of the existing yugabyted server to which the new yugabyted server will join.

--config *config-file*
: Yugabyted configuration file path.

--base_dir *base-directory*
: The directory where yugabyted stores data, configurations, and logs. Must be an absolute path.

--data_dir *data-directory*
: The directory where yugabyted stores data. Must be an absolute path. Can be configured to a directory different from the one where configurations and logs are stored.

--log_dir *log-directory*
: The directory to store yugabyted logs. Must be an absolute path. This flag controls where the logs of the YugabyteDB nodes are stored.

--background *bool*
: Enable or disable running yugabyted in the background as a daemon. Does not persist on restart. Default: `true`

--cloud_location *cloud-location*
: Cloud location of the Yugabyted node in the format `cloudprovider.region.zone`. This information is used for multi-zone, multi-region, and multi-cloud deployments of YugabyteDB clusters.

--fault_tolerance *fault_tolerance*
: Determines the fault tolerance constraint to be applied on the data placement policy of the YugabyteDB cluster. This flag can accept the following values - none, zone, region, and cloud.

--ui *bool*
: Enable or disable the webserver UI. Default: `false`

#### Advanced Flags

Advanced flags can be set by using the configuration file in the `--config` flag. The advanced flags support for the `start` command is as follows:

--ycql_port *ycql-port*
: The port on which YCQL will run.

--ysql_port *ysql-port*
: The port on which YSQL will run.

--master_rpc_port *master-rpc-port*
: The port on which YB-Master will listen for RPC calls.

--tserver_rpc_port *tserver-rpc-port*
: The port on which YB-TServer will listen for RPC calls.

--master_webserver_port *master-webserver-port*
: The port on which YB-Master webserver will run.

--tserver_webserver_port *tserver-webserver-port*
: The port on which YB-TServer webserver will run.

--webserver_port *webserver-port*
: The port on which main webserver will run.

--callhome *bool*
: Enable or disable the *call home* feature that sends analytics data to Yugabyte. Default: `true`.

--master_flags *master_flags*
: Specify extra [master flags](../../../reference/configuration/yb-master#configuration-flags) as a set of key value pairs. Format (key=value,key=value).

--tserver_flags *tserver_flags*
: Specify extra [tserver flags](../../../reference/configuration/yb-tserver#configuration-flags) as a set of key value pairs. Format (key=value,key=value).

--ysql_enable_auth *bool*
: Enable or disable YSQL Authentication. Default: `false`.
: If the `YSQL_PASSWORD` [environment variable](#environment-variables) exists, then authentication mode is automatically set to true.

--use_cassandra_authentication *bool*
: Enable or disable YCQL Authentication. Default: `false`.
: If the `YCQL_USER` or `YCQL_PASSWORD` [environment variables](#environment-variables) exist, then authentication mode is automatically set to true.
: **Note**: The corresponding environment variables have higher priority than the command-line flags.

--initial_scripts_dir *initial-scripts-dir*
: The directory from where yugabyted reads initialization scripts.
: Script format - YSQL `.sql`, YCQL `.cql`.
: Initialization scripts are executed in sorted name order.

#### Deprecated Flags

--daemon *bool*
: Enable or disable running yugabyted in the background as a daemon. Does not persist on restart. Default: `true`.

--listen *bind-ip*
: The IP address or localhost name to which yugabyted will listen.

-----

### configure

Use the `yugabyted configure` command to configure the data placement constraints for the YugabyteDB cluster.

#### Syntax

```text
Usage: yugabyted configure [flags]
```

For example, to create a multi zone YugabyteDB cluster:

```sh
yugabyted configure --fault_tolerance=zone
```

#### Flags

-h | --help
: Print the command line help and exit.

--fault_tolerance *fault_tolerance*
: Specify the fault tolerance for the cluster. This flag can accept one of these values - zone, region, or cloud. For example, when the flag is set to zone (`--fault_tolerance=zone`), yugabyted applies zone fault tolerance to the cluster, placing the nodes in 3 different zones, if available.

--data_placement_constraint *data-placement-constraint*
: Specify the data placement for the YugabyteDB cluster. This is an optional flag. The flag takes comma-seperated values in the format `cloud.region.zone`.

--rf *replication-factor*
: Specify the replication factor for the cluster. This is an optional flag which takes a value of `3` or `5`.

--config *config-file*
: The path to the configuration file of the yugabyted server.

--data_dir *data-directory*
: The data directory for the yugabyted server.

--base_dir *base-directory*
: The base directory for the yugabyted server.

-----

### stop

Use the `yugabyted stop` command to stop a YugabyteDB cluster.

#### Syntax

```sh
Usage: yugabyted stop [-h] [--config CONFIG] [--data_dir DATA_DIR]
                               [--base_dir BASE_DIR]
```

#### Flags

-h | --help
: Print the command line help and exit.

--config *config-file*
: The path to the configuration file of the yugabyted server that needs to be stopped.

--data_dir *data-directory*
: The data directory for the yugabyted server that needs to be stopped.

--base_dir *base-directory*
: The base directory for the yugabyted server that needs to be stopped.

-----

### destroy

Use the `yugabyted destroy` command to delete a cluster.

#### Syntax

```sh
Usage: yugabyted destroy [-h] [--config CONFIG] [--data_dir DATA_DIR]
                                [--base_dir BASE_DIR]
```

#### Flags

-h, --help
: Print the command line help and exit.

--config *config-file*
: The path to the configuration file of the yugabyted server that needs to be destroyed.

--data_dir *data-directory*
: The data directory for the yugabyted server that needs to be destroyed.

--base_dir *base-directory*
: The base directory for the yugabyted server that needs to be destroyed.

-----

### status

Use the `yugabyted status` command to check the status.

#### Syntax

```sh
Usage: yugabyted status [-h] [--config CONFIG] [--data_dir DATA_DIR]
                                 [--base_dir BASE_DIR]
```

#### Flags

-h | --help
: Print the command line help and exit.

--config *config-file*
: The path to the configuration file of the yugabyted server whose status is desired.

--data_dir *data-directory*
: The data directory for the yugabyted server whose status is desired.

--base_dir *base-directory*
: The base directory for the yugabyted server that whose status is desired.

-----

### version

Use the `yugabyted version` command to check the version number.

#### Syntax

```sh
Usage: yugabyted version [-h] [--config CONFIG] [--data_dir DATA_DIR]
                                  [--base_dir BASE_DIR]
```

#### Flags

-h | --help
: Print the command line help and exit.

--config *config-file*
: The path to the configuration file of the yugabyted server whose version is desired.

--data_dir *data-directory*
: The data directory for the yugabyted server whose version is desired.

--base_dir *base-directory*
: The base directory for the yugabyted server that whose version is desired.

-----

### collect_logs

Use the `yugabyted collect_logs` command to generate a zipped file with all logs.

#### Syntax

```sh
Usage: yugabyted collect_logs [-h] [--config CONFIG]
                                       [--data_dir DATA_DIR]
                                       [--base_dir BASE_DIR]
```

#### Flags

-h | --help
: Print the command line help and exit.

--config *config-file*
: The path to the configuration file of the yugabyted server whose logs are desired.

--data_dir *data-directory*
: The data directory for the yugabyted server whose logs are desired.

--base_dir *base-directory*
: The base directory for the yugabyted server whose logs are desired.

-----

### connect

Use the `yugabyted connect` command to connect to the cluster using [ysqlsh](../../../admin/ysqlsh/) or [ycqlsh](../../../admin/ycqlsh).

#### Syntax

```sh
Usage: yugabyted connect [-h] {ycql,ysql} ...
```

#### Flags

-h | --help
: Print the command line help and exit.

--ysql
: Connect with ysqlsh.

--ycql
: Connect with ycqlsh.

-----

### demo

Use the `yugabyted demo connect` command to start YugabyteDB with the [Northwind sample dataset](../../../sample-data/northwind/).

#### Syntax

```sh
Usage: yugabyted demo [-h] {connect,destroy} ...
```

#### Flags

-h | --help
: Print the help message and exit.

connect
: Loads the Northwind sample dataset into a new `yb_demo_northwind` SQL database and then opens the `ysqlsh` prompt for the same database. Skips the data load if data is already loaded.

destroy
: Shuts down the yugabyted single-node cluster and removes data, configuration, and log directories.
: Deletes the `yb_demo_northwind` database.

-----

## Environment Variables

In the case of multi-node deployments, all nodes should have similar environment variables.

Changing the values of the environment variables after the first run has no effect.

### YSQL

Set `YSQL_PASSWORD` to use the cluster in enforced authentication mode.

Combinations of environment variables and their uses.

- `YSQL_PASSWORD`

  Update the default yugabyte user's password.

- `YSQL_PASSWORD, YSQL_DB`

  Update the default yugabyte user's password and create `YSQL_DB` named DB.

- `YSQL_PASSWORD, YSQL_USER`

  Create `YSQL_USER` named user and DB with password `YSQL_PASSWORD`.

- `YSQL_USER`

  Create `YSQL_USER` named user and DB with password `YSQL_USER`.

- `YSQL_USER, YSQL_DB`

  Create `YSQL_USER` named user with password `YSQL_USER` and `YSQL_DB` named DB.

- `YSQL_DB`

  Create `YSQL_DB` named DB.

- `YSQL_USER, YSQL_PASSWORD, YSQL_DB`

  Create `YSQL_USER` named user with password `YSQL_PASSWORD` and `YSQL_DB` named DB.

### YCQL

Set `YCQL_USER` or `YCQL_PASSWORD` to use the cluster in enforced authentication mode.

Combinations of environment variables and their uses.

- `YCQL_PASSWORD`

  Update the default cassandra user's password.

- `YCQL_PASSWORD, YCQL_KEYSPACE`

  Update the default cassandra user's password and create `YCQL_KEYSPACE` named keyspace.

- `YCQL_PASSWORD, YCQL_USER`

  Create `YCQL_USER` named user and DB with password `YCQL_PASSWORD`.

- `YCQL_USER`

  Create `YCQL_USER` named user and DB with password `YCQL_USER`.

- `YCQL_USER, YCQL_KEYSPACE`

  Create `YCQL_USER` named user with password `YCQL_USER` and `YCQL_USER` named keyspace.

- `YCQL_KEYSPACE`

  Create `YCQL_KEYSPACE` named keyspace.

- `YCQL_USER, YCQL_PASSWORD, YCQL_KEYSPACE`

  Create `YCQL_USER` named user with password `YCQL_PASSWORD` and `YCQL_KEYSPACE` named keyspace.

-----

## Examples

### Create a single-node cluster

Create a single-node cluster with a given base directory. Note the need to provide a fully-qualified directory path for the `base_dir` parameter.

```sh
./bin/yugabyted start --advertise_address=127.0.0.1 --base_dir=/Users/username/yugabyte-{{< yb-version version="preview" >}}/data1
```

### Pass additional flags to YB-TServer

Create a single-node cluster and set additional flags for the YB-TServer process.

```sh
./bin/yugabyted start --tserver_flags="pg_yb_session_timeout_ms=1200000,ysql_max_connections=400"
```

### Create a local multi-node cluster

Run the following command:

```sh
./bin/yugabyted start --advertise_address=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node1 --cloud_location=aws.us-east.us-east-1a
```

On MacOS and Linux, the additional nodes need loopback addresses configured:

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```

Add two more nodes to the cluster using the `join` option.

```sh
./bin/yugabyted start --advertise_address=127.0.0.2 --join=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node2 --cloud_location=aws.us-east.us-east-2a
./bin/yugabyted start --advertise_address=127.0.0.3 --join=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node3 --cloud_location=aws.us-east.us-east-3a
```

### Destroy a local multi-node cluster

Destroy the above multi-node cluster.

```sh
./bin/yugabyted destroy --base_dir=/Users/username/yugabyte-{{< yb-version version="preview" >}}/data1
./bin/yugabyted destroy --base_dir=/Users/username/yugabyte-{{< yb-version version="preview" >}}/data2
./bin/yugabyted destroy --base_dir=/Users/username/yugabyte-{{< yb-version version="preview" >}}/data3
```

### Create a multi-zone cluster

To create a multi-node cluster, you start the first node by running the `yugabyted start` command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details, as follows:

```sh
./bin/yugabyted start --advertise_address=<host-ip> --cloud_location=aws.us-east.us-east-1a --fault_tolerance=zone
```

Start the second and the third node on two separate VMs:

```sh
./bin/yugabyted start --advertise_address=<host-ip> --join=<ip-address-first-yugabyted-node> --cloud_location=aws.us-east.us-east-2a --fault_tolerance=zone

./bin/yugabyted start --advertise_address=<host-ip> --join=<ip-address-first-yugabyted-node> --cloud_location=aws.us-east.us-east-3a --fault_tolerance=zone
```

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the cluster:

```sh
./bin/yugabyted configure --fault_tolerance=zone
```

This command automatically determines the data placement constraint based on the `--cloud_location` of each node in the cluster. If there are 3 or more zones available in the cluster, the configure command configures the cluster to survive at least 1 availability zone failure. Otherwise, it outputs a warning message.

The replication factor of the cluster defaults to 3.

You can set the data placement constraint manually using the `--data_placement_constraint` flag, which takes the comma-separated value of `cloud.region.zone`. For example:

```sh
./bin/yugabyted configure --fault_tolerance=zone --data_placement_constraint=aws.us-east.us-east-1a,aws.us-east.us-east-2a,aws.us-east.us-east-3a
```

You can set the replication factor of the cluster manually using the `--rf` flag. For example:

```sh
./bin/yugabyted configure --fault_tolerance=zone --data_placement_constraint=aws.us-east.us-east-1a,aws.us-east.us-east-2a,aws.us-east.us-east-3a --rf=3
```

### Create a multi-region cluster

To create a multi-region cluster, start the first yugabyted node by running the `yugabyted start` command, pass in the `--cloud_location` and `--fault_tolerance` flags to set the node location details as follows:

```sh
./bin/yugabyted start --advertise_address=<host-ip> --cloud_location=aws.us-east.us-east-1a --fault_tolerance=region
```

Start the second and the third nodes on two separate VMs as follows:

```sh
./bin/yugabyted start --advertise_address=<host-ip> --join=<ip-address-first-yugabyted-node> --cloud_location=aws.us-west.us-west-1a --fault_tolerance=region

./bin/yugabyted start --advertise_address=<host-ip> --join=<ip-address-first-yugabyted-node> --cloud_location=aws.us-central.us-central-1a --fault_tolerance=region
```

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the YugabyteDB cluster as follows:

```sh
./bin/yugabyted configure --fault_tolerance=region
```

This command determines the data placement constraint based on the `--cloud_location` of each node in the cluster. If there are 3 or more regions available in the cluster, the command configures the cluster to survive at least 1 region failure. Otherwise, it outputs a warning message.

The replication factor of the cluster defaults to 3.

You can set the data placement constraint manually using the `--data_placement_constraint` flag, which takes the comma-separated value of `cloud.region.zone` as follows:

```sh
./bin/yugabyted configure --fault_tolerance=region --data_placement_constraint=aws.us-east.us-east-1a,aws.us-west.us-west-1a,aws.us-central.us-central-1a
```

You can set the replication factor of the cluster manually using the `--rf` flag as follows:

```sh
./bin/yugabyted configure --fault_tolerance=region --data_placement_constraint=aws.us-east.us-east-1a,aws.us-west.us-west-1a,aws.us-central.us-central-1a --rf=3
```

## Upgrade a YugabyteDB cluster

To use the latest features of the database and apply the latest security fixes, it's prudent to upgrade your YugabyteDB cluster to the [latest release](https://download.yugabyte.com/).

Upgrading an existing YugabyteDB cluster that was deployed using yugabyted includes the following steps:

1. Stop the running YugabyteDB node using the `yugabyted stop` command.

1. Start the new yugabyted process by executing the `yugabyted start` command. Use the previously configured `--base_dir` when restarting the instance.

Repeat the steps on all the nodes of the cluster, one node at a time.

### Upgrade a cluster from single to multi zone

{{< note title="Note" >}}

Multi-zone, multi-region deployment using yugabyted is supported in YugabyteDB 2.15.0.0 and later.

{{< /note >}}

The following steps assume that you have a running YugabyteDB cluster deployed using `yugabyted`, and have [downloaded the update](https://download.yugabyte.com/).

1. Stop the first node by using `yugabyted stop` command:

    ```sh
    ./bin/yugabyted stop
    ```

1. Start the YugabyteDB node by using `yugabyted start` command by providing the necessary cloud information as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=<host-ip> --cloud_location=aws.us-east.us-east-1a --fault_tolerance=zone
    ```

1. Repeat the previous step on all the nodes of the cluster, one node at a time.

1. After starting all nodes, specify the data placement constraint on the cluster using the following command:

    ```sh
    ./bin/yugabyted configure --fault_tolerance=zone
    ```

    To manually specify the data placement constraint, use the following command:

    ```sh
    ./bin/yugabyted configure --fault_tolerance=zone --data_placement_constraint=aws.us-east.us-east-1a,aws.us-east.us-east-2a,aws.us-east.us-east-3a --rf=3
    ```
