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
| --cluster-name | Required. The cluster name to which to assign the allow lists. |
| --network-allow-list | Required. The network allow list to assign to the cluster. |

### allow-list unassign

Unassign an allow list from a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster name from which to unassign the allow lists. |
| --network-allow-list | Required. The network allow list to unassign from the cluster. |

### endpoint list

List the endpoints of the specified cluster. This includes public and private host addresses, and private service endpoints.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster for which to list the endpoints. |
| --region | Return endpoints only from the specified region. |
| --accessibility | Return endpoints only with the specified accessibility type. `PUBLIC`, `PRIVATE`, or `PRIVATE_SERVICE_ENDPOINT`.

### endpoint describe

Fetch detailed information about the specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster for which to list the endpoints. |
| --endpoint-id | Required. The endpoint to describe. |

### endpoint create

Create a private service endpoint for a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster for which to create the endpoint. |
| --region | Required. Region in which to create the endpoint. |
| --accessibility-type | Required. Whether the endpoint is publicly accessible or private. |
| --security-principals | Required. A comma-separated list of AWS Amazon Resource Names (ARNs). |

### endpoint update

Update the configuration of a specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster where the endpoint is configured. |
| --region | Required. Region in which to create the endpoint. |
| --accessibility-type | Required. Whether the endpoint is publicly accessible or private. |
| --security-principals | Required. A comma-separated list of AWS Amazon Resource Names (ARNs). |

### endpoint delete

Delete a specified private service endpoint.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. The cluster for which to list the endpoints. |
| --endpoint-id | Required. The endpoint to delete. |
