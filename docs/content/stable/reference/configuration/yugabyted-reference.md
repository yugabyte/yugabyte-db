---
title: yugabyted command reference
headerTitle: yugabyted command reference
linkTitle: Command reference
description: yugabyted command reference.
headcontent: Utility for deploying and managing YugabyteDB
menu:
  stable:
    identifier: yugabyted-reference
    parent: yugabyted
    weight: 100
type: docs
rightNav:
  hideH4: true
---

For instructions on installing yugabyted, see [Installation](../yugabyted/#installation).

For instructions on using yugabyted to deploy single- and multi-node universes, see [Using yugabyted](../yugabyted/#using-yugabyted).

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

You can access command-line help for yugabyted by running one of the following examples from the YugabyteDB home:

```sh
$ ./bin/yugabyted -h
```

```sh
$ ./bin/yugabyted --help
```

For help with specific yugabyted commands, run 'yugabyted [ command ] -h'. For example, you can print the command-line help for the `yugabyted start` command by running the following:

```sh
$ ./bin/yugabyted start -h
```

### Pass additional flags to YB-Master and YB-TServer

You can set additional configuration options for the YB-Master and YB-TServer processes using the `--master_flags` and `--tserver_flags` flags.

For example, to create a single-node universe and set additional flags for the YB-TServer process, run the following:

```sh
./bin/yugabyted start --tserver_flags="pg_yb_session_timeout_ms=1200000,ysql_max_connections=400"
```

When setting CSV value flags, such as [--ysql_hba_conf_csv](../yb-tserver/#ysql-hba-conf-csv), you need to enclose the values inside curly braces `{}`; if a setting includes double quotes (`"`), precede the double quotes with a backslash (`\`) to make it an escape sequence. For example:

```sh
./bin/yugabyted start --tserver_flags="ysql_hba_conf_csv={host all all 127.0.0.1/0 password,\"host all all 0.0.0.0/0 ldap ldapserver=***** ldapsearchattribute=cn ldapport=3268 ldapbinddn=***** ldapbindpasswd=\"\"*****\"\"\"}"
```

For more information on additional server configuration options, see [YB-Master](../yb-master/) and [YB-TServer](../yb-tserver/).

## Commands

The following commands are available:

- [backup](#backup)
- [cert](#cert)
- [collect_logs](#collect-logs)
- [configure](#configure)
- [configure_read_replica](#configure-read-replica)
- [connect](#connect)
- [demo](#demo)
- [destroy](#destroy)
- [finalize_upgrade](#finalize-upgrade)
- [restore](#restore)
- [start](#start)
- [status](#status)
- [stop](#stop)
- [upgrade](#upgrade)
- [version](#version)
- [xcluster](#xcluster)

-----

### backup

Use the `yugabyted backup` command to take a backup of a YugabyteDB database into a network file storage directory or public cloud object storage.

To use `backup`, the yugabyted node must be started with `--backup_daemon=true` to initialize the backup/restore agent. See the [start](#start) command.

Note that backup and restore are not supported on macOS.

#### Syntax

```text
Usage: yugabyted backup [flags]
```

Examples:

Take a backup into AWS S3 bucket:

```sh
./bin/yugabyted backup --database=yb-demo-northwind --cloud_storage_uri=s3://[bucket_name]
```

Take a backup into Network file storage:

```sh
./bin/yugabyted backup --database=yb-demo-northwind --cloud_storage_uri=/nfs-dir
```

Determine the status of a backup task:

```sh
./bin/yugabyted backup --database=yb-demo-northwind --cloud_storage_uri=s3://[bucket_name] --status
```

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--cloud_storage_uri *cloud_storage_location*
: Cloud location to store the backup data files.

--database *database*
: YSQL Database to be backed up to cloud storage.

--keyspace *keyspace*
: YCQL Keyspace to be backed up to cloud storage.

--status
: Check the status of the backup task.

-----

### cert

Use the `yugabyted cert` command to create TLS/SSL certificates for deploying a secure YugabyteDB universe.

#### Syntax

```text
Usage: yugabyted cert [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted cert` command:

- [generate_server_certs](#generate-server-certs)

#### generate_server_certs

Use the `yugabyted cert generate_server_certs` sub-command to generate keys and certificates for the specified hostnames.

For example, to create node server certificates for hostnames 127.0.0.1, 127.0.0.2, 127.0.0.3, execute the following command:

```sh
./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1,127.0.0.2,127.0.0.3
```

#### Flags

-h | --help
: Print the command-line help and exit.

--hostnames *hostnames*
: Hostnames of the nodes to be added in the universe. Mandatory flag.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) for the yugabyted server.

-----

### collect_logs

Use the `yugabyted collect_logs` command to generate a zipped file with all logs.

#### Syntax

```sh
Usage: yugabyted collect_logs [flags]
```

#### Flags

-h | --help
: Print the command-line help and exit.

--stdout
: Redirect the `logs.tar.gz` file's content to stdout. For example, `docker exec \<container-id\> bin/yugabyted collect_logs --stdout > yugabyted.tar.gz`

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server whose logs are desired.

-----

### configure

Use the `yugabyted configure` command to do the following:

- Configure the data placement policy of the universe.
- Enable or disable encryption at rest.
- Configure point-in-time recovery.
- Run yb-admin commands on a universe.

#### Syntax

```sh
Usage: yugabyted configure [command] [flags]
```

#### Commands

The following sub-commands are available for `yugabyted configure` command:

- [data_placement](#data-placement)
- [encrypt_at_rest](#encrypt-at-rest)
- [point_in_time_recovery](#point-in-time-recovery)
- [admin_operation](#admin-operation)

#### data_placement

Use the `yugabyted configure data_placement` sub-command to set or modify placement policy of the nodes of the deployed universe, and specify the [preferred region(s)](../../../architecture/key-concepts/#preferred-region).

For example, you would use the following command to create a multi-zone YugabyteDB universe:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=zone
```

##### data_placement flags

-h | --help
: Print the command-line help and exit.

--fault_tolerance *fault-tolerance*
: Specify the fault tolerance for the universe. This flag can accept one of the following values: zone, region, cloud. For example, when the flag is set to zone (`--fault_tolerance=zone`), yugabyted applies zone fault tolerance to the universe, placing the nodes in three different zones, if available.

--constraint_value *data-placement-constraint-value*
: Specify the data placement and preferred region(s) for the YugabyteDB universe. This is an optional flag. The flag takes comma-separated values in the format `cloud.region.zone:priority`. The priority is an integer and is optional, and determines the preferred region(s) in order of preference. You must specify the same number of data placement values as the [replication factor](../../../architecture/key-concepts/#replication-factor-rf).

--rf *replication-factor*
: Specify the replication factor for the universe. This is an optional flag which takes a value of `3` or `5`.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

#### encrypt_at_rest

Use the `yugabyted configure encrypt_at_rest` sub-command to enable or disable [encryption at rest](../../../secure/encryption-at-rest/) for the deployed universe.

To use encryption at rest, OpenSSL must be installed on the nodes.

For example, to enable encryption at rest for a deployed YugabyteDB universe, execute the following:

```sh
./bin/yugabyted configure encrypt_at_rest --enable
```

To disable encryption at rest for a YugabyteDB universe which has encryption at rest enabled, execute the following:

```sh
./bin/yugabyted configure encrypt_at_rest --disable
```

##### encrypt_at_rest flags

-h | --help
: Print the command-line help and exit.

--disable
: Disable encryption at rest for the universe. There is no need to set a value for the flag. Use `--enable` or `--disable` flag to toggle encryption features on a YugabyteDB universe.

--enable
: Enable encryption at rest for the universe. There is no need to set a value for the flag. Use `--enable` or `--disable` flag to toggle encryption features on a YugabyteDB universe.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

#### point_in_time_recovery

Use the `yugabyted configure point_in_time_recovery` sub-command to configure a snapshot schedule for a specific database.

Examples:

Enable point-in-time recovery for a database:

```sh
./bin/yugabyted configure point_in_time_recovery --enable --retention <retention_period> --database <database_name>
```

Disable point-in-time recovery for a database:

```sh
./bin/yugabyted configure point_in_time_recovery --disable --database <database_name>
```

Display point-in-time schedules configured on the universe:

```sh
./bin/yugabyted configure point_in_time_recovery --status
```

##### point_in_time_recovery flags

-h | --help
: Print the command-line help and exit.

--database *database*
: Name of the YSQL database for which point-in-time recovery is to be configured.

--keyspace *keyspace*
: Name of the YCQL keyspace for which point-in-time recovery is to be configured.

--enable
: Enable point-in-time recovery for a database or keyspace.

--disable
: Disable point-in-time recovery for a database or keyspace.

--retention *retention-period*
: Specify the retention period in days for the snapshots, after which they will be automatically deleted, from the time they were created.

--status
: Display point-in-time recovery status for a YugabyteDB universe.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

#### admin_operation

Use the `yugabyted configure admin_operation` command to run a yb-admin command on the YugabyteDB universe.

For example, get the YugabyteDB universe configuration:

```sh
./bin/yugabyted configure admin_operation --command 'get_universe_config'
```

##### admin_operation flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--command *yb-admin-command*
: Specify the yb-admin command to be executed on the YugabyteDB universe.

--master_addresses *master-addresses*
: Comma-separated list of current masters of the YugabyteDB universe.

-----

### configure_read_replica

Use the `yugabyted configure_read_replica` command to configure, modify, or delete a [read replica cluster](../../../architecture/key-concepts/#read-replica-cluster).

Before configuring a read replica, you should have a primary cluster deployed, along with the read replica nodes.

#### Syntax

```text
Usage: yugabyted configure_read_replica [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted configure_read_replica` command:

- [new](#new)
- [modify](#modify)
- [delete](#delete)

#### new

Use the sub-command `yugabyted configure_read_replica new` to configure a new read replica cluster.

For example, to create a new read replica cluster, execute the following command:

```sh
./bin/yugabyted configure_read_replica new --rf=1 --data_placement_constraint=cloud1.region1.zone1
```

##### new flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--rf *read-replica-replication-factor*
: Replication factor for the read replica cluster.

--data_placement_constraint *read-replica-constraint-value*
: Data placement constraint value for the read replica cluster. This is an optional flag. The flag takes comma-separated values in the format `cloud.region.zone:num_of_replicas`.

#### modify

Use the sub-command `yugabyted configure_read_replica modify` to modify an existing read replica cluster.

For example, modify a read replica cluster using the following commands.

Change the replication factor of the existing read replica cluster:

```sh
./bin/yugabyted configure_read_replica modify --rf=2

```

Change the replication factor and also specify the placement constraint:

```sh
./bin/yugabyted configure_read_replica modify --rf=2 --data_placement_constraint=cloud1.region1.zone1,cloud2.region2.zone2

```

##### modify flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--rf *read-replica-replication-factor*
: Replication factor for the read replica cluster.

--data_placement_constraint *read-replica-constraint-value*
: Data placement constraint value for the read replica cluster. This is an optional flag. The flag takes comma-separated values in the format cloud.region.zone.

#### delete

Use the sub-command `yugabyted configure_read_replica delete` to delete an existing read replica cluster.

For example, delete a read replica cluster using the following command:

```sh
./bin/yugabyted configure_read_replica delete
```

##### delete flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

-----

### connect

Use the `yugabyted connect` command to connect to the universe using [ysqlsh](../../../api/ysqlsh/) or [ycqlsh](../../../api/ycqlsh/).

#### Syntax

```sh
Usage: yugabyted connect [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted connect` command:

- [ysql](#ysql)
- [ycql](#ycql)

#### ysql

Use the `yugabyted connect ysql` sub-command to connect to YugabyteDB with [ysqlsh](../../../api/ysqlsh/).

#### ysql flags

-h | --help
: Print the command-line help and exit.

--username *username*
: YSQL username to connect to the database.

--password *password*
: The password for YSQL user.

--database *database*
: Name of the YSQL database to connect to.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server to connect to.

#### ycql

Use the `yugabyted connect ycql` sub-command to connect to YugabyteDB with [ycqlsh](../../../api/ycqlsh/).

#### ycql flags

-h | --help
: Print the command-line help and exit.

--username *username*
: YCQL username to connect to the keyspace.

--password *password*
: The password for YCQL user.

--keyspace *keyspace*
: Name of the YCQL keyspace to connect to.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server to connect to.

-----

### demo

Use the `yugabyted demo` command to use the demo [Northwind sample dataset](/stable/develop/sample-data/northwind/) with YugabyteDB.

#### Syntax

```sh
Usage: yugabyted demo [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted demo` command:

- [connect](#connect-1)
- [destroy](#destroy-1)

#### connect

Use the `yugabyted demo connect` sub-command to load the  [Northwind sample dataset](/stable/develop/sample-data/northwind/) into a new `yb_demo_northwind` SQL database, and then open the ysqlsh prompt for the same database.

#### destroy

Use the `yugabyted demo destroy` sub-command to shut down the yugabyted single-node universe and remove data, configuration, and log directories. This sub-command also deletes the `yb_demo_northwind` database.

#### Flags

-h | --help
: Print the help message and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server to connect to or destroy.

-----

### destroy

Use the `yugabyted destroy` command to delete a universe.

#### Syntax

```sh
Usage: yugabyted destroy [flags]
```

For examples, see [Destroy a local universe](../yugabyted/#destroy-a-local-universe).

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server that needs to be destroyed.

-----

### finalize_upgrade

{{< warning title="Performing upgrades" >}}
This command is deprecated. Use 'upgrade finalize_new_version' instead.
{{< /warning >}}

Use the `yugabyted finalize_upgrade` command to finalize and upgrade the AutoFlags and YSQL catalog to the new version and complete the upgrade process.

#### Syntax

```text
Usage: yugabyted finalize_upgrade [flags]
```

For example, finalize the upgrade process after upgrading all the nodes of the YugabyteDB universe to the new version as follows:

```sh
yugabyted finalize_upgrade --upgrade_ysql_timeout <time_limit_ms>
```

Note that `finalize_upgrade` is a cluster-level operation; you don't need to run it on every node.

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--upgrade_ysql_timeout *upgrade_timeout_in_ms*
: Custom timeout for the YSQL upgrade in milliseconds. Default timeout is 60 seconds.

-----

### restore

Use the `yugabyted restore` command to restore a database in the YugabyteDB universe from a network file storage directory or from public cloud object storage.

To use `restore`, the yugabyted node must be started with `--backup_daemon=true` to initialize the backup/restore agent. See the [start](#start) command.

Note that backup and restore are not supported on macOS.

#### Syntax

```text
Usage: yugabyted restore [flags]
```

Examples:

Restore a database from AWS S3 bucket:

```sh
./bin/yugabyted restore --database=yb-demo-northwind --cloud_storage_uri=s3://[bucket_name]
```

Restore a database from a network file storage directory:

```sh
./bin/yugabyted restore --database=yb-demo-northwind --cloud_storage_uri=/nfs-dir
```

Restore the database to a point in time in history:

```sh
./bin/yugabyted restore --database yugabyte --recover_to_point_in_time '2024-01-29 9:30:00 PM'
```

Note: To be able to restore to a point in time, PITR scheduling has to be enabled on the database using `yugabyted configure point_in_time_recovery`.

Determine the status of a restore task:

```sh
./bin/yugabyted restore --database=yb-demo-northwind --cloud_storage_uri=s3://[bucket_name] --status
```

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--cloud_storage_uri *cloud_storage_location*
: Cloud location to store the backup data files.

--database *database*
: YSQL Database to be backed up to cloud storage.

--keyspace *keyspace*
: YCQL Keyspace to be backed up to cloud storage.

--recover_to_point_in_time *pitr*
: Restore to the specified point-in-time with timestamp enclosed in single quotes.

--status
: Check the status of the backup task.

-----

### start

Use the `yugabyted start` command to start a one-node YugabyteDB universe for running [YSQL](../../../api/ysql/) and [YCQL](../../../api/ycql/) workloads in your local environment.

To use encryption in transit, OpenSSL must be installed on the nodes.

If you want to use backup and restore, start the node with `--backup_daemon=true` to initialize the backup and restore agent YB Controller. (YB Controller must be installed; refer to [Installation](../yugabyted/#installation).)

#### Syntax

```text
Usage: yugabyted start [flags]
```

Examples:

Create a local single-node universe:

```sh
./bin/yugabyted start
```

Create a local single-node universe with encryption in transit and authentication:

```sh
./bin/yugabyted start --secure
```

Create a local single-node universe with IPv6 address:

```sh
./bin/yugabyted start --advertise_address ::1
```

Create a single-node locally and join other nodes that are part of the same universe:

```sh
./bin/yugabyted start --join=host:port
```

Create a single-node locally and set advanced flags using a configuration file:

```sh
./bin/yugabyted start --config /path/to/configuration-file
```

For more advanced examples, see the [yugabyted](../yugabyted/) guide.

#### Flags

-h | --help
: Print the command-line help and exit.

--advertise_address *bind-ip*
: IP (v4 or v6) address or local hostname on which yugabyted will listen.

--join *master-ip*
: The IP (v4 or v6) or DNS address of an existing yugabyted server (that is part of a universe) that the new yugabyted server will join, or if the server was restarted, rejoin. The join flag accepts a single IP address, DNS name, or label with correct [DNS syntax](https://en.wikipedia.org/wiki/Domain_Name_System#Domain_name_syntax,_internationalization) (that is, letters, numbers, and hyphens).

--config *path-to-config-file*
: yugabyted advanced configuration file path. Refer to [Use a configuration file](#use-a-configuration-file).

--base_dir *base-directory*
: The directory where yugabyted stores data, configurations, and logs. Must be an absolute path. By default [base directory](../yugabyted/#base-directory) is `$HOME/var`.

--background *bool*
: Enable or disable running yugabyted in the background as a daemon. Does not persist on restart. Default: `true`

--cloud_location *cloud-location*
: Cloud location of the yugabyted node in the format `cloudprovider.region.zone`. This information is used for multi-zone, multi-region, and multi-cloud deployments of YugabyteDB universes.

{{<tip title="Rack awareness">}}
For on-premises deployments, consider racks as zones to treat them as fault domains.
{{</tip>}}

--fault_tolerance *fault_tolerance*
: Determines the fault tolerance constraint to be applied on the data placement policy of the YugabyteDB universe. This flag can accept the following values: none, zone, region, cloud.

--ui *bool*
: Enable or disable the webserver UI (available at <http://localhost:15433>). Default: `true`

--secure
: Enable [encryption in transit](../../../secure/tls-encryption/) and [authentication](../../../secure/enable-authentication/authentication-ysql/) for the node.
: Encryption in transit requires SSL/TLS certificates for each node in the universe.
: - When starting a local single-node universe, a certificate is automatically generated for the universe.
: - When deploying a node in a multi-node universe, you need to generate the certificate for the node using the `--cert generate_server_certs` command and copy it to the node *before* you start the node using the `--secure` flag, or the node creation will fail.
: When authentication is enabled, the default user is `yugabyte` in YSQL, and `cassandra` in YCQL. When a universe is started, yugabyted outputs a message `Credentials File is stored at <credentials_file_path.txt>` with the credentials file location.
: For more information on creating secure clusters, refer to [Manage certificates and authentication](../yugabyted/#manage-certificates-and-authentication).

--read_replica *read_replica_node*
: Use this flag to start a read replica node.

--backup_daemon *backup-daemon-process*
: Enable or disable the backup daemon with yugabyted start. Default: `false`
: Using the `--backup_daemon` flag requires YB Controller; see [Installation](../yugabyted/#installation).

#### Advanced flags

The advanced flags supported by the `start` command are as follows:

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

--data_dir *data-directory*
: The directory where yugabyted stores data. Must be an absolute path. Can be configured to a directory different from the one where configurations and logs are stored. By default, data directory is `<base_dir>/data`.

--log_dir *log-directory*
: The directory to store yugabyted logs. Must be an absolute path. This flag controls where the logs of the YugabyteDB nodes are stored. By default, logs are written to `<base_dir>/logs`.

--certs_dir *certs-directory*
: The path to the directory which has the certificates to be used for secure deployment. Must be an absolute path. Default path is `<base_dir>/certs`.

--master_flags *master_flags*
: Specify extra [master flags](../yb-master/#all-flags) as a set of key value pairs. Format (key=value,key=value).
: To specify any CSV value flags, enclose the values inside curly braces `{}`. Refer to [Pass additional flags to YB-Master and YB-TServer](#pass-additional-flags-to-yb-master-and-yb-tserver).

--tserver_flags *tserver_flags*
: Specify extra [tserver flags](../yb-tserver/#all-flags) as a set of key value pairs. Format (key=value,key=value).
: To specify any CSV value flags, enclose the values inside curly braces `{}`. Refer to [Pass additional flags to YB-Master and YB-TServer](#pass-additional-flags-to-yb-master-and-yb-tserver).

--ysql_enable_auth *bool*
: Enable or disable YSQL authentication. Default: `false`.
: If the `YSQL_PASSWORD` [environment variable](#environment-variables) exists, then authentication mode is automatically set to `true`.

--use_cassandra_authentication *bool*
: Enable or disable YCQL authentication. Default: `false`.
: If the `YCQL_USER` or `YCQL_PASSWORD` [environment variables](#environment-variables) exist, then authentication mode is automatically set to `true`.
: Note that the corresponding environment variables have higher priority than the command-line flags.

--initial_scripts_dir *initial-scripts-dir*
: The directory from where yugabyted reads initialization scripts.
: Script format - YSQL `.sql`, YCQL `.cql`.
: Initialization scripts are executed in sorted name order.

#### Use a configuration file

You can set advanced flags using a configuration file, specified using the `--config` flag. The configuration file is a JSON file with advanced flags and the corresponding values you want to set. For example, you could start a node using a configuration file as follows:

1. Create a configuration file.

    ```sh
    vi ~/yugabyted.conf
    ```

1. Configure the desired advanced flags in the file. For example:

    ```json
    {
        "master_webserver_port": 7100,
        "tserver_webserver_port": 9100,
        "master_flags": "ysql_enable_packed_row=true,ysql_beta_features=true",
        "tserver_flags": "ysql_enable_packed_row=true,ysql_beta_features=true,yb_enable_read_committed_isolation=true,enable_deadlock_detection=true,enable_wait_queues=true"
    }
    ```

1. Start the node using the `--config` flag.

    ```sh
    ./bin/yugabyted start --config ~/yugabyted.conf
    ```

#### Deprecated flags

--daemon *bool*
: Enable or disable running yugabyted in the background as a daemon. Does not persist on restart. Use [--background](#start) instead. Default: `true`.

--listen *bind-ip*
: The IP address or localhost name to which yugabyted will listen.

-----

### status

Use the `yugabyted status` command to check the status.

#### Syntax

```sh
Usage: yugabyted status [flags]
```

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server that you want to get the status of.

-----

### stop

Use the `yugabyted stop` command to stop a YugabyteDB universe.

#### Syntax

```sh
Usage: yugabyted stop [flags]
```

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server that needs to be stopped.

--upgrade *bool*
:  Stop the node for version upgrade. Default: `false`.

-----

### upgrade

Use the `yugabyted upgrade` command to prepare and perform a YugabyteDB major version upgrade.

```sh
Usage: yugabyted upgrade [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted upgrade` command:

- [ysql_catalog](#ysql-catalog)
- [finalize_new_version](#finalize-new-version)
- [check_version_compatibility](#check-version-compatibility)

#### ysql_catalog

Use the sub-command `yugabyted upgrade ysql_catalog` to upgrade the universe YSQL catalog to a newer version. This command needs
to be executed only once per upgrade, from any node in the universe.

For example, to upgrade the YSQL catalog of a universe, you would execute the following command:

```sh
./bin/yugabyted upgrade ysql_catalog
```

##### ysql_catalog flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--timeout *timeout*
: Custom timeout for the YSQL catalog upgrade in milliseconds.

#### finalize_new_version

Use the sub-command `yugabyted upgrade finalize_new_version` to finalize the upgrade of a YugabyteDB Universe. This command needs
to be executed only once per upgrade, after all the nodes in the universe have been upgraded, from any node in the universe.

For example, to finalize an upgrade to a universe, you would execute the following command:

```sh
./bin/yugabyted upgrade finalize_new_version
```

##### finalize_new_version flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--timeout *timeout*
: Custom timeout for the upgrade finalize operation in milliseconds.

#### check_version_compatibility

Use the sub-command `yugabyted upgrade check_version_compatibility` to verify if the existing YugabyteDB universe is compatible with the new version. This command needs to be executed only once per upgrade, from any node in the universe.

For example, to verify the compatibility of a universe with a new version, you would execute the following command:

```sh
./bin/yugabyted upgrade check_version_compatibility
```

##### check_version_compatibility flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--timeout *timeout*
: Custom timeout for the version check in milliseconds.

-----

### version

Use the `yugabyted version` command to check the version number.

#### Syntax

```sh
Usage: yugabyted version [flags]
```

#### Flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server that you want to get the status of.

### xcluster

Use the `yugabyted xcluster` command to set up or delete [xCluster replication](../../../architecture/docdb-replication/async-replication/) between two universes.

#### Syntax

```text
Usage: yugabyted xcluster [command] [flags]
```

#### Commands

The following sub-commands are available for the `yugabyted xcluster` command:

- [create_checkpoint](#create-checkpoint)
- [add_to_checkpoint](#add-to-checkpoint)
- [set_up](#set-up)
- [add_to_replication](#add-to-replication)
- [status](#status-1)
- [delete_replication](#delete-replication)
- [remove_database_from_replication](#remove-database-from-replication)

#### create_checkpoint

Use the sub-command `yugabyted xcluster create_checkpoint` to checkpoint a new xCluster replication between two universes. This command needs to be run from the source universe of the replication.

For example, to create a new xCluster replication, execute the following command:

```sh
./bin/yugabyted xcluster create_checkpoint \
    --replication_id <replication_id> \
    --databases <comma_separated_database_names> \
    [--automatic_mode]
```

The `create_checkpoint` command takes a snapshot of the database and
determines whether any of the databases to be replicated need to be
copied to the target (this is called bootstrapping the databases).

If bootstrapping is required then the `create_checkpoint` command
outputs directions for bootstrapping the relevant databases.

##### create_checkpoint flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--databases *xcluster-databases*
: Comma-separated list of databases to be added to the replication.

--replication_id *xcluster-replication-id*
: A string to uniquely identify the replication.

--automatic_mode  {{<tags/feature/ga idea="153">}}
: Enable automatic mode for the xCluster replication. For more information refer to [Automatic mode](../../../deploy/multi-dc/async-replication/async-transactional-setup-automatic/). If you omit this flag, then semi-automatic mode is used.

#### add_to_checkpoint

Use the sub-command `yugabyted xcluster add_to_checkpoint` to add new databases to an existing xCluster checkpoint between two universes. This command needs to be run from the source universe of the replication.

For example, to add new databases to xCluster replication, first checkpoint them using the following command:

```sh
./bin/yugabyted xcluster add_to_checkpoint \
    --replication_id <replication_id> \
    --databases <comma_separated_database_names>
```

The `add_to_checkpoint` command takes a snapshot of the database and determines whether any of the databases to be added to the replication need to be copied to the target.

If bootstrapping is required then the `add_to_checkpoint` command
outputs directions for bootstrapping the relevant databases.

##### add_to_checkpoint flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--databases *xcluster-databases*
: Comma separated list of databases to be added to existing replication.

--replication_id *xcluster-replication-id*
: Replication ID of the xCluster replication that you are adding databases to.

#### set_up

Use the sub-command `yugabyted xcluster set_up` to set up xCluster replication between two universes. Run this command from the source universe of the replication.

For example, to set up xCluster replication between two universes, run the following command from a node on the source universe:

```sh
./bin/yugabyted xcluster set_up \
    --replication_id <replication_id> \
    --target_address <ip_of_any_target_cluster_node> \
    --bootstrap_done
```

##### set_up flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--target_address *xcluster-target-address*
: IP address of a node in the target universe.

--replication_id *xcluster-replication-id*
: The replication ID of the xCluster replication to be set up.

--bootstrap_done
: Indicates that you have completed bootstrapping of the databases.
: Using this flag indicates that the databases have been copied from source to target. Any changes made to source after `yugabyted xcluster set_up` command will be reflected on the target.

#### add_to_replication

Use the sub-command `yugabyted xcluster add_to_replication` to add databases to an existing xCluster replication. Run this command from the source universe of the replication.

For example, to add new databases to an existing xCluster replication between two universes, run the following command from a node on the source universe:

```sh
./bin/yugabyted xcluster add_to_replication \
    --databases <comma_separated_database_names> \
    --target_address <ip_of_any_target_cluster_node> \
    --replication_id <replication_id> \
    --bootstrap_done
```

##### add_to_replication flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--target_address *xcluster-target-address*
: IP address of a node in the target universe.

--replication_id *xcluster-replication-id*
: Replication ID of the xCluster replication that you are adding databases to.

--databases *xcluster-databases*
: Comma separated list of databases to be added to existing replication.

--bootstrap_done
: Indicates that you have completed bootstrapping of the databases.
: Using this flag indicates that the databases have been copied from source to target. Any changes made to source after `yugabyted xcluster add_to_replication` command will be reflected on the target.

#### status

Use the sub-command `yugabyted xcluster status` to display information about the specified xCluster replications. This command can be run on either the source or target universe.

For example, to display replication information for all xCluster replications to or from a universe, run the following command:

```sh
./bin/yugabyted xcluster status
```

To display the status of a specific xCluster replication, run the following command:

```sh
./bin/yugabyted xcluster status --replication_id <replication_id>
```

##### status flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--replication_id *xcluster-replication-id*
: The replication ID of the xCluster replication whose status you want to output.
: Optional. If not specified, the status of all replications for the universe is displayed.

#### delete_replication

Use the sub-command `yugabyted xcluster delete_replication` to delete an existing xCluster replication. Run this command from the source universe.

For example, delete an xCluster replication using the following command:

```sh
./bin/yugabyted xcluster delete_replication \
    --replication_id <replication_id> \
    --target_address <ip_of_any_target_cluster_node>
```

{{< warning title="Warning" >}}

If you are using automatic mode and your workload is running while you perform this command, the involved target universe databases may be left in an unusable state.  See [Drop xCluster replication group](../../../deploy/multi-dc/async-replication/async-transactional-setup-automatic/#drop-xcluster-replication-group).

{{< /warning >}}

##### delete_replication flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--target_address *xcluster-target-address*
: IP address of a node in the target universe.
: If the target is not available, the output of `yugabyted xcluster delete_replication` will include the command that you will need to run on the target universe (after bringing it back up) to remove the replication from the target.

--replication_id *xcluster-replication-id*
: The replication ID of the xCluster replication to delete.

#### remove_database_from_replication

Use the sub-command `yugabyted xcluster remove_database_from_replication` to remove database(s) from existing xCluster replication. Run this command from the source universe.

For example, remove a database from an xCluster replication using the following command:

```sh
./bin/yugabyted xcluster remove_database_from_replication \
    --databases <comma_separated_database_names> \
    --replication_id <replication_id> \
    --target_address <ip_of_any_target_cluster_node>
```

{{< warning title="Warning" >}}

If you are using automatic mode and your workload is running while you perform this command, the removed target universe databases may be left in an unusable state.  See [Remove a database from a replication group](../../../deploy/multi-dc/async-replication/async-transactional-setup-automatic/#remove-a-database-from-a-replication-group).

{{< /warning >}}

##### remove_database_from_replication flags

-h | --help
: Print the command-line help and exit.

--base_dir *base-directory*
: The [base directory](../yugabyted/#base-directory) of the yugabyted server.

--target_address *xcluster-target-address*
: IP address of a node in the target universe.

--replication_id *xcluster-replication-id*
: Replication ID of the xCluster replication that you are deleting databases from.

--databases *xcluster-databases*
: Comma separated list of databases to be removed from existing replication.

-----

## Environment variables

In the case of multi-node deployments, all nodes should have similar environment variables.

Changing the values of the environment variables after the first run has no effect.

### YSQL

Set `YSQL_PASSWORD` to use the universe in enforced authentication mode.

The following are combinations of environment variables and their uses:

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

Set `YCQL_USER` or `YCQL_PASSWORD` to use the universe in enforced authentication mode.

The following are combinations of environment variables and their uses:

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
