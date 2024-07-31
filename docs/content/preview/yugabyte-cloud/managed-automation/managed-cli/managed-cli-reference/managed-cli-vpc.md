---
title: ybm CLI vpc resource
headerTitle: ybm vpc
linkTitle: vpc
description: YugabyteDB Aeon CLI reference VPC resource.
headcontent: Manage account VPCs
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-vpc
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `vpc` resource to create and delete [VPCs](../../../../cloud-basics/cloud-vpcs/cloud-vpc-intro/).

## Syntax

```text
Usage: ybm vpc [command] [flags]
```

## Example

Create a global VPC on GCP:

```sh
ybm vpc create \
    --name demo-vpc \
    --cloud-provider GCP \
    --global-cidr 10.0.0.0/18
```

## Commands

### create

Create a VPC.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name for the VPC. |
| --cloud-provider | Cloud provider for the VPC. `AWS`, `AZURE`, or `GCP`. |
| --global-cidr | Global CIDR for a GCP VPC. |
| --region | Comma-delimited list of regions for the VPC. |
| --cidr | Comma-delimited list of CIDRs for the regions in the VPC. Only required if `--region` specified. |

### delete

Delete a specified VPC.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name of the VPC. |

### list

List the VPCs configured for your account.

| Flag | Description |
| :--- | :--- |
| --name | Name of a VPC. |
