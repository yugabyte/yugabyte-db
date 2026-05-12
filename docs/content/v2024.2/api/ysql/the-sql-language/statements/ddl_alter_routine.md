---
title: ALTER ROUTINE statement [YSQL]
headerTitle: ALTER ROUTINE
linkTitle: ALTER ROUTINE
description: Change properties of an existing function or procedure using a single statement form.
menu:
  v2024.2_api:
    identifier: ddl_alter_routine
    parent: statements
type: docs
---

{{< tip title="See the dedicated 'User-defined subprograms and anonymous blocks' section." >}}
User-defined functions and procedures are part of a larger area of functionality. See this major section:

- [User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"](../../../user-defined-subprograms-and-anon-blocks/)
{{< /tip >}}

## Synopsis

Use the `ALTER ROUTINE` statement to change properties of an existing **function** or **procedure** identified by name and signature.

- If the name refers to a **procedure**, `ALTER ROUTINE` has the same effect as [`ALTER PROCEDURE`](../ddl_alter_procedure).
- If the name refers to a **function**, `ALTER ROUTINE` has the same effect as [`ALTER FUNCTION`](../ddl_alter_function).

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

## Semantics

Behavior, supported attributes, and catalog effects follow the same rules as [`ALTER PROCEDURE`](../ddl_alter_procedure) or [`ALTER FUNCTION`](../ddl_alter_function), depending on whether the resolved object is a procedure or a function. Attribute semantics are described in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).

For additional detail, see the [PostgreSQL documentation][postgresql-docs-alter-routine].

## Supported actions

The following forms are accepted (subject to the same restrictions as on `ALTER FUNCTION` / `ALTER PROCEDURE` for the kind of routine):

- `ALTER ROUTINE` ... `RENAME TO` _new_name_
- `ALTER ROUTINE` ... `SET SCHEMA` _new_schema_
- `ALTER ROUTINE` ... `SET` _configuration_parameter_ `=` { _new_value_ | `DEFAULT` } [ `RESTRICT` ]
- `ALTER ROUTINE` ... `SET` _configuration_parameter_ `FROM CURRENT` [ `RESTRICT` ]
- `ALTER ROUTINE` ... `RESET` { _configuration_parameter_ | `ALL` } [ `RESTRICT` ]
- `ALTER ROUTINE` ... [ `EXTERNAL` ] `SECURITY` { `INVOKER` | `DEFINER` } [ `RESTRICT` ]
- `ALTER ROUTINE` ... [ `NO` ] `DEPENDS ON EXTENSION` _extension_name_
- `ALTER ROUTINE` ... `OWNER TO` { _new_owner_ | `CURRENT_ROLE` | `CURRENT_USER` | `SESSION_USER` }

## Example

The following is equivalent to using `ALTER PROCEDURE` when `s1.p` is a procedure (see [`ALTER PROCEDURE`](../ddl_alter_procedure) for a full walkthrough, including catalog checks):

```plpgsql
alter routine s1.p(int) rename to q;
```

## See also

- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`ALTER FUNCTION`](../ddl_alter_function)

[postgresql-docs-alter-routine]: https://www.postgresql.org/docs/15/sql-alterroutine.html
