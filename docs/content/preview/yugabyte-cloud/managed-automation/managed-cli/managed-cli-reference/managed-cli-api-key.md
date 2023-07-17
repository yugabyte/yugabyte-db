---
title: api-key resource
headerTitle: ybm api-key
linkTitle: api-key
description: YugabyteDB Managed CLI reference api-key resource.
headcontent: Manage API keys
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-api-key
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `api-key` resource to create and manage YugabyteDB Managed account [API keys](../../../managed-apikeys/).

## Syntax

```text
Usage: ybm api-key [command] [flags]
```

## Examples

Create an API key in YugabyteDB Managed:

```sh
ybm api-key create \
    --name developer \
    --duration 6 --unit Months \
    --description "Developer API key" \
    --role-name Developer
```

List API keys in YugabyteDB Managed:

```sh
ybm api-key list
```

## Commands

### create

Create an API key.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name for the API key. |
| --duration | Required. The duration for which the API Key will be valid. 0 denotes that the key will never expire. |
| --unit | Required. The time unit for the duration for which the API Key will be valid. `Hours`, `Days`, or `Months`. |
| --description | Description for the API key. |
| --role-name | The name of the role to assign to the API Key. If not provided, the Admin role is assigned. |

### list

List the API keys in your YugabyteDB Managed account.

| Flag | Description |
| :--- | :--- |
| --name | Return only API keys with the specified name. |
| --status | Return only API keys with the specified status. `ACTIVE`, `EXPIRED`, or `REVOKED`. |

### revoke

Revoke a specified API key.

| Flag | Description |
| :--- | :--- |
| --name | Required. Name of the API key to revoke. |
