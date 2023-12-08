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

Use the `user` resource to invite and manage YugabyteDB Managed account [users](../../../../managed-security/manage-access/).

## Syntax

```text
Usage: ybm user [command] [flags]
```

## Examples

List users in YugabyteDB Managed:

```sh
ybm user list
```

Invite a user to YugabyteDB Managed:

```sh
ybm user invite --email developer@mycompany.com --role Developer
```

## Commands

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
