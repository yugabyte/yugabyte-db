---
title: ybm CLI permission resource
headerTitle: ybm permission
linkTitle: permission
description: YugabyteDB Aeon CLI reference Permission resource.
headcontent: List permissions
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-permission
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `permission` resource to list available permissions that can be assigned to [roles](../../../../managed-security/managed-roles/).

## Syntax

```text
Usage: ybm permission [command] [flags]
```

## Examples

List permissions in YugabyteDB Aeon:

```sh
ybm permission list
```

## Commands

### list

List the permissions in your YugabyteDB Aeon account.
