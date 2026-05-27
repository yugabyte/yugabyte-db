---
title: ALTER ROUTINE statement [YSQL]
headerTitle: ALTER ROUTINE
linkTitle: ALTER ROUTINE
description: Change properties of an existing routine (function or procedure).
menu:
  stable_api:
    identifier: ddl_alter_routine
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER ROUTINE` statement to change properties of an existing routine. A _routine_ is either a function or a procedure.

## Syntax

{{%ebnf%}}
  alter_routine,
  routine_signature,
  arg_mode,
  arg_name,
  arg_type,
  alterable_fn_and_proc_attribute
{{%/ebnf%}}

You must identify the routine to be altered by its name, schema, and signature. The signature must include the data types of the routine's arguments.

## Description

`ALTER ROUTINE` is a variant of `ALTER FUNCTION` and `ALTER PROCEDURE` that can apply to either functions or procedures without specifying which type. It is useful when you need to alter a routine but don't know or care whether it is a function or procedure.

The same operations available in [`ALTER FUNCTION`](../ddl_alter_function) and [`ALTER PROCEDURE`](../ddl_alter_procedure) are supported:

- `RENAME TO` — Rename the routine
- `SET SCHEMA` — Move the routine to a different schema
- `OWNER TO` — Change the owner
- `SET configuration_parameter` — Set a runtime configuration parameter
- `RESET configuration_parameter` — Reset a configuration parameter
- `SECURITY { INVOKER | DEFINER }` — Change the security context
- `[NO] DEPENDS ON EXTENSION` — Mark dependency on an extension

## Example

Rename a routine (works for both functions and procedures):

```sql
CREATE FUNCTION f(int) RETURNS int LANGUAGE sql AS 'SELECT $1 * 2';

-- Rename without needing to know if it's a function or procedure
ALTER ROUTINE f(int) RENAME TO double_it;

-- Move to a different schema
CREATE SCHEMA utils;
ALTER ROUTINE double_it(int) SET SCHEMA utils;

-- Change security
ALTER ROUTINE utils.double_it(int) SECURITY DEFINER;
```

## See also

- [`ALTER FUNCTION`](../ddl_alter_function)
- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`CREATE PROCEDURE`](../ddl_create_procedure)
