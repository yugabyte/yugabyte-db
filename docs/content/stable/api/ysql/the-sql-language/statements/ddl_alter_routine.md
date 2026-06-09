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

{{< tip title="See the dedicated 'User-defined subprograms and anonymous blocks' section." >}}
User-defined functions and procedures are part of a larger area of functionality. See this major section:

- [User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"](../../../user-defined-subprograms-and-anon-blocks/)
{{< /tip >}}

## Synopsis

Use the `ALTER ROUTINE` statement to change properties of an existing routine. A _routine_ is either a function or a procedure.

## Syntax

{{%ebnf%}}
  alter_routine,
  subprogram_signature,
  arg_decl,
  special_fn_and_proc_attribute,
  alterable_fn_and_proc_attribute,
  alterable_fn_only_attribute,
  volatility,
  on_null_input,
  parallel_mode
{{%/ebnf%}}

You must identify the routine by:

- Its name and the schema where it lives. This can be done by using its fully qualified name or by using just its bare name and letting name resolution find it in the first schema on the _search_path_ where it occurs. Notice that you don't need to (and cannot) mention the name of its owner.

- Its signature. The _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ is sufficient; and this is typically used. You can use the full _subprogram_signature_. But you should realize that the _formal_arg_ and _arg_mode_ for each _arg_decl_ carry no identifying information. (This is why it is not typically used when a function or procedure is to be altered or dropped.) This is explained in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

## Description

`ALTER ROUTINE` is a variant of `ALTER FUNCTION` and `ALTER PROCEDURE` that can apply to either functions or procedures without specifying which type. It is useful when you need to alter a routine but don't know or care whether it is a function or procedure.

### Supported actions

The same operations available in [ALTER FUNCTION](../ddl_alter_function) and [ALTER PROCEDURE](../ddl_alter_procedure) are supported. The following forms are accepted (subject to the same restrictions as on `ALTER FUNCTION` / `ALTER PROCEDURE` for the kind of routine):

- `ALTER ROUTINE` ... `RENAME TO` _new_name_ : Rename the routine.
- `ALTER ROUTINE` ... `SET SCHEMA` _new_schema_ : Move the routine to a different schema.
- `ALTER ROUTINE` ... `SET` _configuration_parameter_ `=` { _new_value_ | `DEFAULT` } [ `RESTRICT` ] : Set a configuration parameter on the routine to a specific value. The caller's value is saved on entry and restored when the routine returns.
- `ALTER ROUTINE` ... `SET` _configuration_parameter_ `FROM CURRENT` [ `RESTRICT` ] : Set a configuration parameter on the routine to the session's current value at the time of the `ALTER` statement.
- `ALTER ROUTINE` ... `RESET` _configuration_parameter_ [ `RESTRICT` ] : Remove a configuration-parameter setting from the routine.
- `ALTER ROUTINE` ... `RESET ALL` [ `RESTRICT` ] : Remove all configuration-parameter settings from the routine.
- `ALTER ROUTINE` ... [ `EXTERNAL` ] `SECURITY` { `INVOKER` | `DEFINER` } [ `RESTRICT` ] : Change the security context.
- `ALTER ROUTINE` ... [ `NO` ] `DEPENDS ON EXTENSION` _extension_name_ : Mark dependency on an extension.
- `ALTER ROUTINE` ... `OWNER TO` { _new_owner_ | `CURRENT_ROLE` | `CURRENT_USER` | `SESSION_USER` }: Change the routine's owner.

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

- [ALTER FUNCTION](../ddl_alter_function)
- [ALTER PROCEDURE](../ddl_alter_procedure)
- [CREATE FUNCTION](../ddl_create_function)
- [CREATE PROCEDURE](../ddl_create_procedure)
