---
title: vpc peering resource
headerTitle: ybm vpc peering
linkTitle: vpc peering
description: YugabyteDB Managed CLI reference VPC Peering resource.
headcontent: Manage account peering connections
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-peering
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `vpc peering` resource to perform operations on [peering connections](../../../../cloud-basics/cloud-vpcs/cloud-vpc-intro/), including the following:

- create and delete peering connections
- get information about a peering connection

## Syntax

```text
Usage: ybm vpc peering [command] [flags]
```

## Example

Create a peering connection on GCP:

```sh
ybm vpc peering create \
  --name demo-peer \
  --yb-vpc-name demo-vpc \
  --cloud-provider GCP \
  --app-vpc-project-id project \
  --app-vpc-name application-vpc-name \
  --app-vpc-cidr 10.0.0.0/18
```

## Commands

### create

Create a peering connection.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name for the peering. |
| --yb-vpc-name | Required. Name of the YugabyteDB VPC to be peered. |
| --cloud-provider | Required. Cloud provider of the VPC being peered. `AWS` or `GCP`.
| --app-vpc-project-id | Required for GCP. Project ID of the application VPC being peered. GCP only. |
| --app-vpc-name | Required for GCP. Name of the application VPC being peered. GCP only. |
| --app-vpc-cidr | Required for AWS. CIDR of the application VPC. Optional for GCP.
| --app-vpc-account-id | Required for AWS. Account ID of the application VPC. AWS only. |
| --app-vpc-id | Required for AWS. ID of the application VPC. AWS only. |
| --app-vpc-region | Required for AWS. Region of the application VPC. AWS only. |

### delete

Delete a specified peering connection.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name of the peering connection. |

### list

List the VPC peering connections configured for your account.

| Flag | Description |
| :--- | :--- |
| --name | Name of a peering. |
