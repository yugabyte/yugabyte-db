---
title: cluster resource
headerTitle: ybm cluster
linkTitle: cluster
description: YugabyteDB Managed CLI reference Cluster resource.
headcontent: Manage clusters
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-cluster
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `cluster` resource to perform operations on a YugabyteDB Managed cluster, including the following:

- create, update, and delete clusters
- pause and resume clusters
- get information about clusters
- download the cluster certificate

## Syntax

```text
Usage: ybm cluster [command] [flags]
```

## Examples

Create a local single-node cluster:

```sh
ybm cluster create \
  --cluster-name=test-cluster \
  --credentials=username=admin,password=password123
```

Create a multi-node cluster:

```sh
ybm cluster create \
  --cluster-name=test-cluster \
  --credentials=username=admin,password=password123 \
  --cloud-type=GCP \
  --node-config=num-cores=2,disk-size-gb=500 \
  --region-info=region=aws.us-east-2.us-east-2a,vpc=aws-us-east-2 \
  --region-info=region=aws.us-east-2.us-east-2b,vpc=aws-us-east-2 \
  --region-info=region=aws.us-east-2.us-east-2c,vpc=aws-us-east-2 \
  --fault-tolerance=zone
```

## Commands

### create

Create a cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name for the cluster. |
| --credentials | Required. Database credentials for the default user, provided as key-value pairs.<br>Arguments:<ul><li>username</li><li>password</li></ul> |
| --cloud-provider | Cloud provider. `AWS` or `GCP` (default).
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| --node-config | Number of vCPUs and disk size per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li></ul>If specified, num-cores is mandatory, disk-size-gb is optional. |
| --region-info | Region details for multi-region cluster, provided as key-value pairs.<br>Arguments:<ul><li>region-name - name of the region specified as cloud.region</li><li>num_nodes - number of nodes for the region</li><li>vpc - name of the VPC</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region and num-nodes is mandatory, vpc is optional. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --fault-tolerance | Fault tolerance for the cluster. `NONE`, `ZONE`, or `REGION`. |
| --database-version | Database version to use for the cluster. `Stable` or `Preview`. |

### delete

Delete the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Name of the cluster. |

### describe

Fetch detailed information about the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Name of the cluster. |

### list

List all the clusters to which you have access.

| Flag | Description |
| :--- | :--- |
| --cluster-name | The name of the cluster to filter. |

### update

Update the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to update. |
| --cloud-provider | Cloud provider. `AWS` or `GCP`. |
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| --node-config | Number of vCPUs and disk size per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li></ul>If specified, num-cores is mandatory, disk-size-gb is optional. |
| --region-info | Region details for multi-region cluster, provided as key-value pairs.<br>Arguments:<ul><li>region-name - name of the region specified as cloud.region</li><li>num_nodes - number of nodes for the region</li><li>vpc - name of the VPC</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region and num-nodes is mandatory, vpc is optional. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --fault-tolerance | Fault tolerance for the cluster. `NONE`, `ZONE`, or `REGION`. |
| --database-version | Database version to use for the cluster. `Stable` or `Preview`. |

### pause

Pause the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to pause. |

### resume

Resume the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to resume. |

### cert download

Download the [cluster certificate](../../../../cloud-secure-clusters/cloud-authentication/) to a specified location.

| Flag | Description |
| :--- | :--- |
| --force | Overwrite the output file if it exists. |
| --out | Full path with file name of the location to which to download the cluster certificate file. Default is stdout. |
