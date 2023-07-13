---
title: cluster network resource
headerTitle: ybm cluster network
linkTitle: cluster network
description: YugabyteDB Managed CLI reference Cluster network resource.
headcontent: Manage cluster network resources
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-network
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `cluster network` resource to manage cluster network resources, including:

- add [IP allow lists](../../../../cloud-secure-clusters/add-connections/) to clusters
- list cluster endpoints
- create, update, and delete cluster [private service endpoints](../../../../cloud-basics/cloud-vpcs/cloud-add-endpoint/)

## Syntax

```text
Usage: ybm cluster network [command] [flags]
```

## Example

Assign an allow list:

```sh
ybm cluster network allow-list assign \
  --cluster-name=<cluster_name> \
  --network-allow-list=<allow_list_name>
```

## Commands

### allow-list assign

Assign an allow list to a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster to which you want to assign the allow lists. |
| --network-allow-list | Required. The network allow list to assign to the cluster. |

### allow-list unassign

Unassign an allow list from a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster from which you want to unassign the allow lists. |
| --network-allow-list | Required. The network allow list to unassign from the cluster. |

### endpoint create

Create a [private service endpoint](../../../../cloud-basics/cloud-vpcs/cloud-add-endpoint/) for a specified cluster.

| Flag | Description |
| :--- | :--- |
| --accessibility-type | Required. The type of endpoint to create. `PUBLIC`, `PRIVATE`, or `PRIVATE_SERVICE_ENDPOINT`. |
| --cluster-name | Required. The name of the cluster for which you want to create the endpoint. |
| --region | Required. Region in which you want to create the endpoint. |
| --security-principals | Required for `PRIVATE_SERVICE_ENDPOINT`. A comma-separated list of security principals to be granted access to this endpoint. For AWS, these are the Amazon resource names (ARNs) of AWS principals with permissions to create an interface VPC endpoint to connect to your endpoint service. For Azure, provide subscription IDs of the services to be granted access to this endpoint. |

### endpoint delete

Delete a specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster with the endpoint to delete. |
| --endpoint-id | Required. The ID of the endpoint to delete. |

To avoid charges from your cloud provider, be sure to delete the corresponding endpoint in your cloud provider account.

### endpoint describe

Fetch detailed information about a specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster with the endpoint to describe. |
| --endpoint-id | Required. The ID of the endpoint to describe. |

### endpoint list

List the network endpoints of the specified cluster. This includes public and private host addresses, and private service endpoints.

| Flag | Description |
| :--- | :--- |
| --accessibility-type | Return endpoints only with the specified accessibility type. `PUBLIC`, `PRIVATE`, or `PRIVATE_SERVICE_ENDPOINT`.
| --cluster-name | Required. The name of the cluster for which you want to list the endpoints. |
| --region | Return endpoints only from the specified region. |

### endpoint update

Update the configuration of a specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The name of the cluster with the endpoint to update. |
| --endpoint-id | Required. The ID of the endpoint to update. |
| --security-principals | A comma-separated list of security principals to be granted access to this endpoint. For AWS, these are the Amazon resource names (ARNs) of AWS principals with permissions to create an interface VPC endpoint to connect to your endpoint service. For Azure, provide subscription IDs of the services to be granted access to this endpoint. |
