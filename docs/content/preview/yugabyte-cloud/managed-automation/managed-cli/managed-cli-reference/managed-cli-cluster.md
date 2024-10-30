---
title: ybm CLI cluster resource
headerTitle: ybm cluster
linkTitle: cluster
description: YugabyteDB Aeon CLI reference Cluster resource.
headcontent: Manage clusters
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-cluster
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `cluster` resource to perform operations on a YugabyteDB Aeon cluster, including the following:

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
| --cloud-provider | Cloud provider. `AWS` (default), `AZURE`, `GCP`. |
| --cluster-name | Required. Name for the cluster. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| --credentials | Required. Database credentials for the default user, provided as key-value pairs.<br>Arguments:<ul><li>username</li><li>password</li></ul> |
| &#8209;&#8209;database&#8209;version | Database version to use for the cluster. `Innovation`, `Production`, `Preview`, or `'Early Access'`. |
| --default-region | The primary region in a [partition-by-region](../../../../cloud-basics/create-clusters/create-clusters-geopartition/) cluster. The primary region is where all the tables not created in a tablespace reside. |
| --encryption-spec | CMK credentials for encryption at rest, provided as key-value pairs.<br>Arguments:<ul><li>cloud-provider - cloud provider (`AWS`, `AZURE`, or `GCP`); required</li></ul>Required for AWS only:<ul><li>aws-access-key - access key ID</li><li>aws-secret-key - secret access key</li><li>aws-arn - Amazon resource name of the CMK</li></ul>If not provided, you are prompted for the secret access key. AWS secret access key can also be configured using the YBM_AWS_SECRET_KEY [environment variable](../../managed-cli-overview/#environment-variables).<br><br>Required for GCP only:<ul><li>gcp-resource-id - cloud KMS resource ID</li><li>gcp-service-account-path - path to the service account credentials key file</li></ul>Required for Azure only:<ul><li>azu-client-id - client ID of registered application</li><li>azu-client-secret - client secret of registered application</li><li>azu-tenant-id - Azure tenant ID</li><li>azu-key-name - key name</li><li>azu-key-vault-uri - key vault URI in the form `https://myvault.vault.azure.net`</li></ul> |
| --fault-tolerance | Fault domain for the cluster. `NONE`, `NODE`, `ZONE`, or `REGION`. |
| --node-config <br> [Deprecated in v0.1.19] | Number of vCPUs, disk size, and IOPS per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>If specified, num-cores is required and disk-size-gb and disk-iops are optional. |
| --num-faults-to-tolerate | The number of fault domain failures. 0 for NONE; 1 for ZONE; 1, 2, or 3 for NODE and REGION. Default is 1 (or 0 for NONE). |
| --preferred-region | The [preferred region](../../../../cloud-basics/create-clusters/create-clusters-multisync/#preferred-region) in a multi-region cluster. Specify the name of the region. |
| --region-info | Required. Region details for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>region - name of the region</li><li>num-nodes - number of nodes for the region</li><li>vpc - name of the VPC</li><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region, num-nodes, num-cores, disk-size-gb are required. |

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
| &#8209;&#8209;encryption&#8209;spec | CMK credentials for encryption at rest, provided as key-value pairs.<br>Arguments:<ul><li>cloud-provider - cloud provider (`AWS`, `AZURE`, or `GCP`); required</li></ul>Required for AWS only:<ul><li>aws-access-key - access key ID</li><li>aws-secret-key - secret access key</li><li>aws-arn - Amazon resource name of the CMK</li></ul>If not provided, you are prompted for the secret access key. AWS secret access key can also be configured using the YBM_AWS_SECRET_KEY [environment variable](../../managed-cli-overview/#environment-variables).<br><br>Required for GCP only:<ul><li>gcp-resource-id - cloud KMS resource ID</li><li>gcp-service-account-path - path to the service account credentials key file</li></ul>Required for Azure only:<ul><li>azu-client-id - client ID of registered application</li><li>azu-client-secret - client secret of registered application</li><li>azu-tenant-id - Azure tenant ID</li><li>azu-key-name - key name</li><li>azu-key-vault-uri - key vault URI in the form `https://myvault.vault.azure.net`</li></ul> |

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
| --cloud-provider | Cloud provider. `AWS`, `AZURE`, or `GCP`. |
| --cluster-tier | Type of cluster. `Sandbox` or `Dedicated`. |
| --cluster-type | Deployment type. `SYNCHRONOUS` or `GEO_PARTITIONED`. |
| &#8209;&#8209;database&#8209;version | Database version to use for the cluster. `Innovation`, `Production`, `Preview`, or `'Early Access'`. |
| --fault-tolerance | Fault domain for the cluster. `NONE`, `NODE`, `ZONE`, or `REGION`. |
| --new-name | The new name for the cluster. |
| --node-config <br> [Deprecated in v0.1.19] | Number of vCPUs and disk size per node for the cluster, provided as key-value pairs.<br>Arguments:<ul><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>If specified, num-cores is required and disk-size-gb and disk-iops are optional. |
| --num-faults-to-tolerate | The number of fault domain failures. 0 for NONE; 1 for ZONE; 1, 2, or 3 for NODE and REGION. Default is 1 (or 0 for NONE). |
| --region-info | Region details for multi-region cluster, provided as key-value pairs.<br>Arguments:<ul><li>region - name of the region</li><li>num-nodes - number of nodes for the region</li><li>vpc - name of the VPC</li><li>num-cores - number of vCPUs per node</li><li>disk-size-gb - disk size in GB per node</li><li>disk-iops - disk IOPS per node (AWS only)</li></ul>Specify one `--region-info` flag for each region in the cluster.<br>If specified, region, num-nodes, num-cores, disk-size-gb are required. |
