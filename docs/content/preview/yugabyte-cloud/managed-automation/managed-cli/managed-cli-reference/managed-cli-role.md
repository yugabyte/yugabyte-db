---
title: role resource
headerTitle: ybm role
linkTitle: role
description: YugabyteDB Managed CLI reference Role resource.
headcontent: Manage roles
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-role
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `role` resource to create and manage YugabyteDB Managed account [roles](../../../../cloud-admin/managed-roles/).

## Syntax

```text
Usage: ybm role [command] [flags]
```

## Examples

List roles in YugabyteDB Managed:

```sh
ybm role list
```

List AWS instance types in us-west-2:

```sh
ybm region instance list \
  --cloud-provider AWS \
  --region us-west-2
```

## Commands

### create

Create a custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name for the role. |
| --permissions | Required. Permissions for the role, provided as key value pairs. <br>Arguments:<ul><li>resource-type=<resource-type></li><li>operation-group=<operation-group></li></ul>Both resource-type and operation-group are mandatory. Information about multiple permissions can be specified by using multiple --permissions arguments. |
| --description | Description for the role. |

### delete

Delete a specified custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |

### describe

Fetch detailed information about a specified role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |

### list

List the roles available for the specified cluster.

| Flag | Description |
| :--- | :--- |
| --role-name | Name of a role. |
| --type | Type of role. BUILT-IN or CUSTOM. |

### update

Update the specified custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |
| --permissions | Required. Permissions for the role, provided as key value pairs. resource-type=<resource-type>,operation-group=<operation-group> as the value. Both resource-type and operation-group are mandatory. Information about multiple permissions can be specified by using multiple --permissions arguments. |
| --new-name | New name for the updated role. |
