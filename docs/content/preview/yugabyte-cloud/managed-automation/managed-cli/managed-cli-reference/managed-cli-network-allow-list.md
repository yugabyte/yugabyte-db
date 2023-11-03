---
title: network-allow-list resource
headerTitle: ybm network-allow-list
linkTitle: network-allow-list
description: YugabyteDB Managed CLI reference Network allow list resource.
headcontent: Manage account IP allow lists
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-network-allow-list
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `network-allow-list` resource to perform operations on [allow lists](../../../../cloud-secure-clusters/add-connections/), including the following:

- create and delete allow lists
- get information about an IP allow list

## Syntax

```text
Usage: ybm network-allow-list [command] [flags]
```

## Example

Create a single address allow list:

```sh
ybm network-allow-list create \
    --name=my-computer \
    --description="my IP address" \
    --ip-addr=$(curl ifconfig.me)
```

## Commands

### create

Create an allow list.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name for the IP allow list. |
| --description | Description of the IP allow list.<br>If the description includes spaces, enclose the description in quotes ("). |
| --ip-addr | IP addresses to add to the allow list. |

### delete

Delete the specified allow list.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name of the IP allow list to delete. |

### list

List the allow lists configured for your account.

| Flag | Description |
| :--- | :--- |
| --name | Name of an IP allow list. |
