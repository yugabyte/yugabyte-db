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
- encrypt clusters and manage encryption

## Syntax

```text
Usage: ybm cluster [command] [flags]
```

## Examples

Create a local single-node cluster:

```sh
ybm cluster create \
  --cluster-name test-cluster \
  --credentials username=admin,password=password123
```

Create a multi-node cluster:

```sh
ybm cluster create \
  --cluster-name test-cluster \
  --credentials username=admin,password=password123 \
  --cloud-provider AWS \
  --node-config num-cores=2,disk-size-gb=500 \
  --region-info region=ap-northeast-1,num-nodes=1 \
  --region-info region=us-west-1,num-nodes=1 \
  --region-info region=us-west-2,num-nodes=1 \
  --fault-tolerance=ZONE
```

## Commands

### cert download

Download the [cluster certificate](../../../../cloud-secure-clusters/cloud-authentication/) to a specified location.

| Flag | Description |
| :--- | :--- |
| --force | Overwrite the output file if it exists. |
| --out | Full path with file name of the location to which to download the cluster certificate file. Default is `stdout`. |

### create

Create a cluster.

| Flag | Description |
| :--- | :--- |
| --cloud-provider | Cloud provider. `AWS` (default) or `GCP`.
| --cluster-name | Required. Name for the cluster. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| --credentials | Required. Database credentials for the default user, provided as key-value pairs.<br>Arguments:<ul><li>username</li><li>password</li></ul> |
| --database-version | Database version to use for the cluster. `Innovation`, `Production`, or `Preview`. |
| --encryption-spec | Customer managed key (CMK) credentials for encryption at rest, provided as key-value pairs.<br>Arguments:<ul><li>cloud-provider - cloud provider (`AWS`, `AZURE`, or `GCP`); required</li><li>aws-access-key - access key ID (AWS only; required)</li><li>aws-secret-key - secret access key (AWS only)</li><li>aws-arn - Amazon resource name of the CMK (AWS only; required)</li><li>gcp-resource-id - cloud KMS resource ID (GCP only; required)</li><li>gcp-service-account-path - path to the service account credentials key file (GCP only; required)</li><li>azu-client-id - client ID of registered application (Azure only; required)</li><li>azu-client-secret - client secret of registered application (Azure only; required)</li><li>azu-tenant-id - Azure tenant ID (Azure only; required)</li><li>azu-key-name - key name (Azure only; required)</li><li>azu-key-vault-uri - key vault URI in the form `https://myvault.vault.azure.net` (Azure only; required)</li></ul>If not provided, you are prompted for the secret access key.</br>AWS secret access key can also be configured using the YBM_AWS_SECRET_KEY [environment variable](../../managed-cli-overview/#environment-variables). |
| --fault-tolerance | Fault tolerance for the cluster. `NONE`, `NODE`, `ZONE`, or `REGION`. |
| --node-config | Number of vCPUs, disk size, and IOPS per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>If specified, num-cores is required and disk-size-gb and disk-iops are optional. |
| --num-faults-to-tolerate | The number of faults to tolerate. 0 for NONE; 1 for ZONE; 1, 2, or 3 for NODE and REGION. Default is 1 (or 0 for NONE). |
| --region-info | Region details for multi-region cluster, provided as key-value pairs.<br>Arguments:<ul><li>region - name of the region</li><li>num-nodes - number of nodes for the region</li><li>vpc - name of the VPC</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region and num-nodes is required, vpc is optional. |

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

### encryption list

List the encryption at rest configuration for the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster. |

### encryption update

Update the credentials to use for the customer managed key (CMK) used to encrypt the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --encryption-spec | CMK credentials for encryption at rest, provided as key-value pairs.<br>Arguments:<ul><li>cloud-provider - cloud provider (`AWS` or `GCP`); required</li><li>aws-access-key - access key ID (AWS only; required)</li><li>aws-secret-key - secret access key (AWS only)</li><li>aws-arn - Amazon resource name of the CMK (AWS only; required)</li><li>gcp-resource-id - cloud KMS resource ID (GCP only; required)</li><li>gcp-service-account-path - path to the service account credentials key file (GCP only; required)</li><li>azu-client-id - client ID of registered application (Azure only; required)</li><li>azu-client-secret - client secret of registered application (Azure only; required)</li><li>azu-tenant-id - Azure tenant ID (Azure only; required)</li><li>azu-key-name - key name (Azure only; required)</li><li>azu-key-vault-uri - key vault URI in the form `https://myvault.vault.azure.net` (Azure only; required)</li></ul>If not provided, you are prompted for the secret access key.</br>AWS secret access key can also be configured using the YBM_AWS_SECRET_KEY [environment variable](../../managed-cli-overview/#environment-variables). |

### list

List all the clusters to which you have access.

| Flag | Description |
| :--- | :--- |
| --cluster-name | The name of the cluster to filter. |

### network

Refer to [cluster network](../managed-cli-network/).

### node list

List all the nodes in the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster to list nodes for. |

### pause

Pause the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to pause. |

### read-replica

Refer to [cluster read-replica](../managed-cli-read-replica/).

### resume

Resume the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to resume. |

### update

Update the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to update. |
| --cloud-provider | Cloud provider. `AWS` or `GCP`. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| --database-version | Database version to use for the cluster. `Stable` or `Preview`. |
| --fault-tolerance | Fault tolerance for the cluster. `NONE`, `NODE`, `ZONE`, or `REGION`. |
| --new-name | The new name for the cluster. |
| --node-config | Number of vCPUs and disk size per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>If specified, num-cores is required and disk-size-gb and disk-iops are optional. |
| --region-info | Region details for multi-region cluster, provided as key-value pairs.<br>Arguments:<ul><li>region - name of the region</li><li>num-nodes - number of nodes for the region</li><li>vpc - name of the VPC</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region and num-nodes is required, vpc is optional. |
