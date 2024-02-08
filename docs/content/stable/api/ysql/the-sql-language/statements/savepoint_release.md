---
title: RELEASE SAVEPOINT statement [YSQL]
headerTitle: RELEASE SAVEPOINT
linkTitle: RELEASE SAVEPOINT
description: Use the `RELEASE SAVEPOINT` statement to release a savepoint.
menu:
  stable:
    identifier: savepoint_release
    parent: statements
type: docs
---

## Synopsis

Use the `RELEASE SAVEPOINT` statement to release the server-side state associated with tracking a savepoint and make the named savepoint no longer accessible to [`ROLLBACK TO`](../savepoint_rollback).

## Syntax

{{%ebnf%}}
  savepoint_release
{{%/ebnf%}}

## Semantics

### *release*

```plpgsql
RELEASE [ SAVEPOINT ] name
```

#### NAME

The name of the savepoint you wish to release.

{{<note title="Other savepoints may be released">}}
When you release a savepoint, all savepoints that were created after it was created are also released.
{{</note>}}


## Examples

Begin a transaction and create a savepoint.

```plpgsql
BEGIN TRANSACTION;
SAVEPOINT test;
```

Once you are done with it, release the savepoint:

```plpgsql
RELEASE test;
```

If at this point, you attempt to rollback to `test`, it will be an error:

```plpgsql
ROLLBACK TO test;
```

```output
ERROR:  savepoint "test" does not exist
```

## See also

- [`SAVEPOINT`](../savepoint_create)
- [`ROLLBACK TO`](../savepoint_rollback)
