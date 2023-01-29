---
title: YugabyteDB Managed CLI
headerTitle: YugabyteDB Managed CLI
linkTitle: YugabyteDB Managed CLI
description: Use YugabyteDB Managed CLI to access YugabyteDB clusters.
headcontent: Manage cluster and account resources from the command line
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

The YugabyteDB Managed Command Line Interface (YBM CLI) is an open source tool that enables you to interact with YugabyteDB Managed accounts using commands in your command-line shell. With minimal configuration, the CLI enables you to start running commands that implement functionality equivalent to that provided by the browser-based YugabyteDB Managed interface from the command prompt in your shell.

You can install the YugabyteDB Managed CLI using any of the following methods:

Using Docker:

```sh
docker run -it yugabytedb/yugabyte-client ybm-cli -h <hostname> -p <port>
```

Using Homebrew:

```sh
brew tap yugabyte/yugabytedb
brew install ybm-cli
```

Using a shell script:

```sh
$ curl -sSL https://downloads.yugabyte.com/get_ybm_cli.sh | bash
```

If you have wget, you can use the following:

```sh
wget -q -O - https://downloads.yugabyte.com/get_ybm_cli.sh | sh
```

## Setup

Using ybm-cli requires providing, at minimum, an API key and the host address.

You can pass these values as flags. For example:

```sh
ybm --apikey AWERDFSSS --host cloud.yugabyte.com cluster get
```

For convenience, configure `ybm-cli` with default values for these flags as follows:

- Using a configuration file called `.ybm-cli.yaml` under your `$HOME` directory. You can use the command `ybm configure` to set up the file.

- Using [environment variables](#environment-variables). Environment variables must begin with `YBM_`. For example:

  ```sh
  export YBM_APIKEY=AWERDFSSS
  export YBM_HOST=cloud.yugabyte.com
  ybm get cluster
  ```

By default, `https://` is added to the host if no scheme is provided. To use http, add `http://` to the host.

## Syntax

```sh
ybm [-h] [ <resource> ] [ <command> ] [ <flags> ]
```

- resource: resource to be changed
- command: command to run
- flags: one or more flags, separated by spaces.

### Example

```sh
$ ybm cluster create
```

### Online help

You can access command-line help for `ybm` by running one of the following examples from the YugabyteDB home:

```sh
ybm -h
```

```sh
ybm -help
```

For help with specific `ybm` resource commands, run 'ybm [ resource ] [ command ] -h'. For example, you can print the command-line help for the `ybm create` command by running the following:

```sh
ybm read-replica create -h
```

## Resources

The following resources can be managed using the CLI:

- [cluster](#cluster)
- [network-allow-list](#network-allow-list)
- [read-replica](#read-replica)
- [vpc](#vpc)
- [vpc-peering](#vpc-peering)
- [cdc-sink](#cdc-sink)

-----

### cluster

Use the `cluster` resource to perform operations on a YugabyteDB cluster.

```text
Usage: ybm cluster [command] [flags]
```

Examples:

- Create a local single-node cluster:

  ```sh
  ybm cluster create 
      --cluster-name=test-cluster
      --credentials=username=anonymous,password=password123
  ```

#### create

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

--region-info=region=<region-name>,num_nodes=<number-of-nodes>,vpc=<vpc-name>
: Region details for multi-region cluster.

--cluster-tier=_tier_
: Type of cluster; `free` or `paid`.

--fault-tolerance=_tolerance_
: Fault tolerance for the cluster. `none`, `zone`, or `region`.

--database-track=_track_
: Database version to use for the cluster. `stable` or `preview`.

#### delete

--cluster-name=_name_
: Name of the cluster.

-----

### network-allow-list

Use the `network-allow-list` resource to perform operations on a YugabyteDB cluster allow list.

```text
Usage: ybm network-allow-list [command] [flags]
```

Examples:

- Create a single address allow list:

  ```sh
  ybm network-allow-list create 
      --name=test-cluster
      --description="my IP address"
      --ip_addr=0.0.0.0/0
  ```

#### create

--name=_name_
: Name for the IP allow list.

--description=_description_
: Description of the IP allow list. If the description includes spaces, enclose the description in quotes (").

--ip_addr=_ip address_
: IP address to add to the allow list.

-----

### read-replica

Use the `read-replica` resource to perform operations on a YugabyteDB cluster read replica.

```text
Usage: ybm read-replica [command] [flags]
```

Examples:

- Create a read-replica cluster:

  ```sh
  ybm read-replica create 
     --replica=num_cores=<region-num_cores>,memory_mb=<memory_mb>,disk_size_gb=<disk_size_gb>,code=<GCP or AWS>,region=<region>,num_nodes=<num_nodes>,vpc=<vpc_name>,num_replicas=<num_replicas>,multi_zone=<multi_zone>
  ```

#### create

--replica=_arguments_
: Specifications for the read replica.

-----

### vpc

Use the `vpc` resource to perform operations on VPCs.

```text
Usage: ybm vpc [command] [flags]
```

Examples:

- Create a global VPC on GCP:

  ```sh
  ybm vpc create
      --name=demo-vpc
      --cloud=GCP
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

-----

### vpc-peering

Use the `vpc-peering` resource to perform operations on VPC peerings.

```text
Usage: ybm vpc-peering [command] [flags]
```

Examples:

- Create a VPC peering on GCP:

  ```sh
  ybm vpc-peering create
      --name=demo-peer
      --vpc-name=demo-vpc
      --cloud=GCP
      --project=project
      --vpc=vpc-name
      --region=us-west1
      --cidr=10.0.0.0/18
  ```

#### create

--name=_name_
: Name for the peering.

--vpc-name=_name_
: Name of the VPC to be peered.

--cloud=_provider_ (this is cloud-type above)
: Cloud provider. `aws` or `gcp`.

--project=_project ID_
: Project.

--vpc=_name_
: The YugabyteDB Managed VPC to peer to.

--region=_region(s)_
: Comma-delimited list of regions for the VPC.

--cidr=_CIDR_
: CIDR of the peered (application) VPC.

-----

### cdc-sink

Use the `cdc-sink` resource to perform operations on CDC sinks.

```text
Usage: ybm cdc-sink [command] [flags]
```

Examples:

- Create a CDC sink:

  ```sh
  ybm cdc_sink create
      --name=sink-2 
      --hostname=kafka.self.us 
      --auth-type=BASIC 
      --cdc-sink-type=KAFKA 
      --username=something 
      --password=something
  ```

#### create

--name=_name_
: Name for the sink.

--hostname=_host_
: Hostname of the CDC sink.

--auth-type=_authorization_
: Authorization type for the sink. `basic`

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

#### delete

--name=_name_
: Name of the sink.

## Environment variables

In the case of multi-node deployments, all nodes should have similar environment variables.

The following are combinations of environment variables and their uses:

- `YBM_APIKEY`

  The API key to use to authenticate to your YugabyteDB Managed account.

- `YBM_HOST`

  The YugabyteDB Managed host.

## Examples

### Create a single-node cluster

### Create a multi-node cluster
