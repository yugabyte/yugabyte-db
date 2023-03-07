---
title: YugabyteDB Managed CLI
headerTitle: YugabyteDB Managed CLI
linkTitle: YugabyteDB Managed CLI
description: Use YugabyteDB Managed CLI to access YugabyteDB clusters.
headcontent: Manage cluster and account resources from the command line
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli
    parent: managed-automation
    weight: 20
type: docs
rightNav:
  hideH4: true
---

## Overview

The [YugabyteDB Managed Command Line Interface](https://github.com/yugabyte/ybm-cli) (ybm) is an open source tool that enables you to interact with YugabyteDB Managed accounts using commands in your command-line shell. With minimal configuration, the CLI enables you to start running commands that implement functionality equivalent to that provided by the browser-based YugabyteDB Managed interface from the command prompt in your shell.

You can install the ybm CLI using Homebrew:

```sh
brew install yugabyte/yugabytedb/ybm
```

## Global configuration

Using ybm CLI requires providing, at minimum, an [API key](../managed-apikeys/) and the host address.

You can pass these values as flags when running ybm commands. For example:

```sh
ybm --apikey AWERDFSSS --host cloud.yugabyte.com cluster get
```

For convenience, you can configure ybm with default values for these flags as follows:

- Use the `auth` command to write these values to a YAML configuration file. For example:

  ```sh
  ybm auth --apikey AWERDFSSS --host cloud.yugabyte.com
  ```

  By default, this writes the values to the file `.ybm-cli.yaml` under your `$HOME` directory.

  To switch between multiple configurations while using ybm, use the `--config` flag to specify the configuration file. For example, to use a configuration file named `.ybm-cli-portal.yaml` instead of the default configuration, execute the following command:

  ```sh
  ybm --config ~/.ybm-cli-portal.yaml cluster get
  ```

- Using [environment variables](#environment-variables). Environment variables must begin with `YBM_`. For example:

  ```sh
  export YBM_APIKEY=AWERDFSSS
  export YBM_HOST=cloud.yugabyte.com
  ybm cluster get
  ```

By default, `https://` is added to the host if no scheme is provided. To use http, add `http://` to the host.

### Autocompletion

You can configure command autocompletion for your shell using the `completion` resource. For example:

```sh
ybm completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

## Syntax

```sh
ybm [-h] [ <resource> ] [ <command> ] [ <flags> ]
```

- resource: resource to be changed
- command: command to run
- flags: one or more flags, separated by spaces.

For example:

```sh
ybm cluster create
```

### Online help

You can access command-line help for `ybm` by running the following commands from YugabyteDB home:

```sh
ybm -h
```

```sh
ybm --help
```

For help with specific `ybm` resource commands, run `ybm [ resource ] [ command ] -h`. For example, you can print the command-line help for the `ybm create` command by running the following:

```sh
ybm read-replica create -h
```

## Resources

The following resources can be managed using the CLI:

- [backup](#backup)
- [cluster](#cluster)
- [network-allow-list](#network-allow-list)
- [read-replica](#read-replica)
- [vpc](#vpc)
- [vpc-peering](#vpc-peering)

<!--
- [cdc-sink](#cdc-sink)
- [cdc-stream](#cdc-stream) -->

-----

### backup

Use the `backup` resource to perform operations on cluster backups, including the following:

- create and delete cluster backups
- restore a backup
- get information about clusters

```text
Usage: ybm backup [command] [flags]
```

Examples:

- Create a backup:

  ```sh
  ybm backup create \
      --cluster-name=test-cluster \
      --credentials=username=admin,password=password123
  ```

#### create

--cluster-name=_name_
: Name of the cluster to back up.

--retention-period=_days_
: Retention period for the backup in days (integer). Default is 1.

--description=_description_
: A description of the backup.

#### delete

--backup-id=_id_
: The ID of the backup to delete.

#### get

--cluster-name=_name_
: Name of the cluster of which you want to view the backups.

#### restore

--cluster-name=_name_
: Name of the cluster to restore to.

--backup-id=_id_
: The ID of the backup to restore.

### cluster

Use the `cluster` resource to perform operations on a YugabyteDB cluster, including the following:

- create, update, and delete clusters
- pause and resume clusters
- get information about clusters
- add IP allow lists to clusters

```text
Usage: ybm cluster [command] [flags]
```

Examples:

- Create a local single-node cluster:

  ```sh
  ybm cluster create \
      --cluster-name=test-cluster \
      --credentials=username=admin,password=password123
  ```

#### create

Create a cluster.

--cluster-name=_name_
: Name for the cluster.

--credentials=_username=name,password=password_
: The database credentials for the default user.

--cloud-type=_provider_
: Cloud provider. `aws` or `gcp`.

--cluster-type=_type_
: Deployment type. `synchronous` or `geo_partitioned`.

--node-config=_number_
: Number of nodes for the cluster

--region-info=region=_region-name_,num_nodes=_number-of-nodes_,vpc=_vpc-name_
: Region details for multi-region cluster, provided as key-value pairs.
: Specify one `--region-info` flag for each region in the cluster.

--cluster-tier=_tier_
: Type of cluster; `sandbox` or `dedicated`.

--fault-tolerance=_tolerance_
: Fault tolerance for the cluster. `none`, `zone`, or `region`.

--database-track=_track_
: Database version to use for the cluster. `stable` or `preview`.

#### delete

Delete the specified cluster.

--cluster-name=_name_
: Name of the cluster.

#### get

Fetch information about the specified cluster.

--cluster-name=_name_
: Name of the cluster.

#### update

--cluster-name=_name_
: Name of the cluster to update.

--cloud-type=_provider_
: Cloud provider. `aws` or `gcp`.

--cluster-type=_type_
: Deployment type. `synchronous` or `geo_partitioned`.

--node-config=_number_
: Number of nodes for the cluster

--region-info=region=_region-name_,num_nodes=_number-of-nodes_,vpc=_vpc-name_
: Region details for multi-region cluster, provided as key-value pairs.
: Specify one `--region-info` flag for each region in the cluster.

--cluster-tier=_tier_
: Type of cluster; `sandbox` or `dedicated`.

--fault-tolerance=_tolerance_
: Fault tolerance for the cluster. `none`, `zone`, or `region`.

--database-track=_track_
: Database version to use for the cluster. `stable` or `preview`.

#### pause

--cluster-name=_name_
: Name of the cluster to pause.

#### resume

--cluster-name=_name_
: Name of the cluster to resume.

#### describe-regions

Equivalent of `cloud-regions get`.

#### describe-instances

Equivalent of `instance-types get`.

#### add-network-allow-list

Equivalent of `network-allow-list assign`.

-----

### network-allow-list

Use the `network-allow-list` resource to perform operations on a YugabyteDB cluster allow list, including the following:

- create and delete allow lists
- get information about an IP allow list

```text
Usage: ybm network-allow-list [command] [flags]
```

Examples:

- Create a single address allow list:

  ```sh
  ybm network-allow-list create \
      --name=test-cluster \
      --description="my IP address" \
      --ip_addr=0.0.0.0/0
  ```

#### create

--name=_name_
: Name for the IP allow list.

--description=_description_
: Description of the IP allow list. If the description includes spaces, enclose the description in quotes (").

--ip_addr=_ip address_
: IP addresses to add to the allow list.

#### delete

--name=_name_
: Name of the IP allow list to delete.

#### get

--name=_name_
: Name of the IP allow list.

-----

### read-replica

Use the `read-replica` resource to perform operations on a YugabyteDB cluster read replica, including the following:

- create, update, and delete read replicas
- get information about read replicas

```text
Usage: ybm read-replica [command] [flags]
```

Examples:

- Create a read-replica cluster:

  ```sh
  ybm read-replica create \
    --replica=num_cores=<region-num_cores>,\
    memory_mb=<memory_mb>,\
    disk_size_gb=<disk_size_gb>,\
    code=<GCP or AWS>,\
    region=<region>,\
    num_nodes=<num_nodes>,\
    vpc=<vpc_name>,\
    num_replicas=<num_replicas>,\
    multi_zone=<multi_zone>
  ```

#### create

--cluster-name=_name_
: Name of the cluster to which you want to add read replicas.

--replica=_arguments_
: Specifications for the read replica provided as key-value pairs, as follows:

- num_cores - number of vCPUs per node
- memory_mb - memory (MB) per node
- disk_size_gb - disk size (GB) per node
- code - cloud provider (`aws` or `gcp`)
- region - region in which to deploy the read replica
- num_nodes - number of nodes for the read replica
- vpc_name - name of the VPC in which to deploy the read replica
- num_replicas - the replication factor
- multi-zone - whether the read replica is multi-zone.

#### delete

--cluster-name=_name_
: Name of the cluster whose read replicas you want to delete.

#### get

--cluster-name=_name_
: Name of the cluster whose read replicas you want to fetch.

#### update

--cluster-name=_name_
: Name of the cluster whose read replicas you want to update.

--replica=_arguments_
: Specifications for the read replica provided as key-value pairs, as follows:

- num_cores - number of vCPUs per node
- memory_mb - memory (MB) per node
- disk_size_gb - disk size (GB) per node
- code - cloud provider (`aws` or `gcp`)
- region - region in which to deploy the read replica
- num_nodes - number of nodes for the read replica
- vpc_name - name of the VPC in which to deploy the read replica
- num_replicas - the replication factor
- multi-zone - 

-----

### vpc

Use the `vpc` resource to create and delete VPCs.

```text
Usage: ybm vpc [command] [flags]
```

Examples:

- Create a global VPC on GCP:

  ```sh
  ybm vpc create \
      --name=demo-vpc \
      --cloud=GCP \
      --global-cidr=10.0.0.0/18
  ```

#### create

--name=_name_
: Name for the VPC.

--cloud=_provider_
: Cloud provider. `aws` or `gcp`.

--region=_region(s)_
: Comma-delimited list of regions for the VPC.

--global-cidr=_CIDR_
: Global CIDR for a GCP VPC.

--cidr=_CIDRs_
: Comma-delimited list of CIDRs for the regions in the VPC. Only required if `--region` specified.

#### delete

--name=_name_
: Name of the VPC.

#### get

--name=_name_
: Name of the VPC.

-----

### vpc-peering

Use the `vpc-peering` resource to perform operations on VPC peerings, including the following:

- create and delete VPC peerings
- get information about a peering

```text
Usage: ybm vpc-peering [command] [flags]
```

Examples:

- Create a VPC peering on GCP:

  ```sh
  ybm vpc-peering create \
      --name=demo-peer \
      --vpc-name=demo-vpc \
      --cloud=GCP \
      --project=project \
      --vpc=vpc-name \
      --region=us-west1 \
      --cidr=10.0.0.0/18
  ```

#### create

--name=_name_
: Name for the peering.

--yb-vpc-name=_name_
: Name of the YugabyteDB VPC to be peered.

--cloud=_provider_ (this is cloud-type above)
: Cloud provider. `aws` or `gcp`.

--app-vpc-project-id=_project ID_
: Project ID of the application VPC being peered. GCP only; required.

--app-vpc-name=_name_
: Name of the application VPC being peered. GCP only; required.

--app-vpc-cidr=_CIDR_
: CIDR of the application VPC. Required for AWS; optional for GCP.

--app-vpc-account-id=_ID_
: Account ID of the application VPC. AWS only; required.

--app-vpc-id=_ID_
: ID of the application VPC. AWS only; required.

--app-vpc-region=_region(s)_
: Regions of the application VPC. AWS only; required.

#### delete

--name=_name_
: Name of the peering.

#### get

--name=_name_
: Name of the peering.

-----
<!--
### cdc-sink

Use the `cdc-sink` resource to create, update, and delete CDC sinks.

```text
Usage: ybm cdc-sink [command] [flags]
```

Examples:

- Create a CDC sink:

  ```sh
  ybm cdc_sink create \
      --name=sink-2 \
      --hostname=kafka.self.us \
      --auth-type=BASIC \
      --cdc-sink-type=KAFKA \
      --username=admin \
      --password=password
  ```

#### create

--name=_name_
: Name for the sink.

--hostname=_host_
: Hostname of the CDC sink.

--auth-type=_authorization_
: Authorization type of the sink. `basic`

--cdc-sink-type=_type_
: Type of CDC sink.

--username=_name_
: Sink user name.

--password=_password_
: Sink user password.

#### get

--name=_name_
: Name of the sink.

#### update

--name=_name_
: Name of the sink.

--new-name=_name_
: New name for the sink.

--username=_name_
: Sink user name.

--password=_password_
: Sink user password.

#### delete

--name=_name_
: Name of the sink.

-----

### cdc-stream

Use the `cdc-stream` resource to create, update, and delete CDC streams.

```text
Usage: ybm cdc-stream [command] [flags]
```

Examples:

- Create a CDC stream:

  ```sh
  ybm cdc-stream create \
      --cluster-name=cluster-1 \
      --name=stream-2 \
      --tables=table1,table2 \
      --sink=mysink \
      --db-name=mydatabase \
      --snapshot-existing-data=true \
      --kafka-prefix=prefix
  ```

#### create

--cluster-name=_name_
: Name of the cluster with the tables you want to stream.

--name=_name_
: Name for the stream.

--tables=_table names_
: List of tables the CDC stream will listen to.

--sink=_sink_
: Destination sink for the stream.

--db-name=_database name_
: Database that the Cdc Stream will listen to.

--snapshot-existing-data=_bool_
: Whether to snapshot the existing data in the database.

--kafka-prefix=_prefix_
: Prefix for the Kafka topics.

#### get

--cluster-name=_name_
: Name of the cluster with the streams you want to fetch.

--name=_name_
: Name of the CDC stream.

#### update

--cluster-name=_name_
: Name of the cluster with the tables you want to stream.

--name=_name_
: Name of the stream.

--tables=_table names_
: List of tables the CDC stream will listen to.

--new-name=_name_
: New name for the stream.

#### delete

--cluster-name=_name_
: Name of the cluster with the stream to delete.

--name=_name_
: Name of the stream.

----->

### wait flag

For long-running commands such as creating or deleting a cluster, you can use the `--wait` flag to wait until the operation is completed. For example:

```sh
ybm cluster delete \
    --cluster-name=test-cluster \
    --wait
```

If you are using ybm with the `--wait` flag in your CI system, you can set the environment variable `YBM_CI` to `true` to avoid generating unnecessary log lines.

## Environment variables

In the case of multi-node deployments, all nodes should have similar environment variables.

The following are combinations of environment variables and their uses:

- `YBM_APIKEY`

  The API key to use to authenticate to your YugabyteDB Managed account.

- `YBM_HOST`

  The YugabyteDB Managed host.

- `YBM_CI`

  Set to `true` to avoid outputting unnecessary log lines.

## Examples

### Create a single-node cluster

```sh
ybm cluster create \
    --cluster-name=test-cluster \
    --credentials=username=admin,password=password123
```

### Create a multi-node cluster

```sh
ybm cluster create \
    --cluster-name=test-cluster \
    --credentials=username=admin,password=password123 \
    --cloud-type=gcp \
    --node-config=3 \
    --region-info=region=aws.us-east-2.us-east-2a,vpc=aws-us-east-2 \
    --region-info=region=aws.us-east-2.us-east-2b,vpc=aws-us-east-2 \
    --region-info=region=aws.us-east-2.us-east-2c,vpc=aws-us-east-2 \
    --fault-tolerance=zone \
    --credentials=username=admin,password=password123
```

### Create an IP allow list and add your computer

```sh
ybm network-allow-list create \
  --ip-addr $(curl ifconfig.me) \
  --name "my computer" \
  --description "Access the cluster from the CLI"
```

### Assign an IP allow list to a cluster

```sh
ybm cluster assign \
  --cluster-name test-cluster\
  --network-allow-list "my computer""
```
