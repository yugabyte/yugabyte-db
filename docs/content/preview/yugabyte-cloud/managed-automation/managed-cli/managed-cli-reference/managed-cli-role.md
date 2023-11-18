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

Use the `role` resource to create and manage YugabyteDB Managed account [roles](../../../../managed-security/managed-roles/).

## Syntax

```text
Usage: ybm role [command] [flags]
```

## Examples

List roles in YugabyteDB Managed:

```sh
ybm role list
```

Describe the Admin role:

```sh
ybm role describe --role-name admin
```

You can use this command to view all the available permissions.

Create a role:

```sh
ybm role create --role-name backuprole \
  --permissions resource-type=BACKUP,operation-group=CREATE \
  --permissions resource-type=BACKUP,operation-group=DELETE
```

## Commands

### create

Create a custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name for the role. |
| &#8209;&#8209;permissions | Required. Permissions for the role, provided as key value pairs. Permissions are made up of a resource type and an operation group.<br>Arguments:<ul><li>resource-type - the resource to which the permission applies</li><li>operation-group - the operation that the permission allows on the resource</li></ul>Both resource-type and operation-group are mandatory. Specify multiple permissions by using multiple --permissions arguments. Use `ybm permission list` to view a list of all permissions. |
| --description | Description for the role. |

### delete

Delete a specified custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |

### describe

Fetch detailed information about a specified role, including its permissions (name, description, resource type, and operation group), and users and API keys that are assigned to the role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |

### list

List the roles in your YugabyteDB Managed account.

| Flag | Description |
| :--- | :--- |
| --role-name | Name of a role. |
| --type | Type of role. BUILT-IN or CUSTOM. |

### update

Update the specified custom role.

| Flag | Description |
| :--- | :--- |
| --role-name | Required. Name of the role. |
| &#8209;&#8209;permissions | Required. Permissions for the role, provided as key value pairs. Permissions are made up of a resource type and an operation group.<br>Arguments:<ul><li>resource-type - the resource to which the permission applies</li><li>operation-group - the operation that the permission allows on the resource</li></ul>Both resource-type and operation-group are mandatory. Specify multiple permissions by using multiple --permissions arguments. Use `ybm permission list` to view a list of all permissions. |
| --description | Description for the role. |
| --new-name | New name for the updated role. |
