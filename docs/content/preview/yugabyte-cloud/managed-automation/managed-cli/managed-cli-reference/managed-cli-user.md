---
title: user resource
headerTitle: ybm user
linkTitle: user
description: YugabyteDB Managed CLI reference user resource.
headcontent: Manage users
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-user
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `user` resource to create and manage YugabyteDB Managed account [users](../../../../cloud-admin/manage-access/).

## Syntax

```text
Usage: ybm user [command] [flags]
```

## Examples

List users in YugabyteDB Managed:

```sh
ybm user list
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

Delete a specified user.

| Flag | Description |
| :--- | :--- |
| --email | Required. Email of the user. |

### invite

Invite a user to your YugabyteDB Managed account.

| Flag | Description |
| :--- | :--- |
| --email | Required. Email of the user. |
| --role-name | Required. Name of the role to assign. |

### list

List the users in your YugabyteDB Managed account.

| Flag | Description |
| :--- | :--- |
| --email | Email of a user. |

### update

Modify the role of a user in your YugabyteDB Managed account.

| Flag | Description |
| :--- | :--- |
| --email | Required. Email of the user. |
| --role-name | Required. Name of the role to assign. |
