---
title: ALTER PROCEDURE statement [YSQL]
headerTitle: ALTER PROCEDURE
linkTitle: ALTER PROCEDURE
description: Change properties of an existing procedure.
menu:
  stable_api:
    identifier: ddl_alter_procedure
    parent: statements
type: docs
---

{{< tip title="See the dedicated 'User-defined subprograms and anonymous blocks' section." >}}
User-defined procedures are part of a larger area of functionality. See this major section:

- [User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"](../../../user-defined-subprograms-and-anon-blocks/)
{{< /tip >}}

## Synopsis

Use the `ALTER PROCEDURE` statement to change properties of an existing procedure.

## Syntax

{{%ebnf%}}
  alter_procedure,
  subprogram_signature,
  arg_decl,
  special_fn_and_proc_attribute,
  alterable_fn_and_proc_attribute
{{%/ebnf%}}

You must identify the to-be-altered procedure by:

- Its name and the schema where it lives. This can be done by using its fully qualified name or by using just its bare name and letting name resolution find it in the first schema on the _search_path_ where it occurs. Notice that you don't need to (and cannot) mention the name of its owner.

- Its signature. The _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ is sufficient; and this is typically used. You can use the full _subprogram_signature_. But you should realize that the _formal_arg_ and _arg_mode_ for each _arg_decl_ carry no identifying information. (This is why it is not typically used when a function or procedure is to be altered or dropped.) This is explained in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

## Semantics

This is explained in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).

## Example

Supposed that you create a procedure like this:

```plpgsql
drop schema if exists s1 cascade;
drop schema if exists s2 cascade;
create schema s1;

create procedure s1.p(i in int)
  security definer
  language plpgsql
as $body$
begin
  execute format('set my_namespace.x = %L', i::text);
end;
$body$;

call s1.p(42);
select current_setting('my_namespace.x')::int as "my_namespace.x";
```

This is the result:

```output
 my_namespace.x
----------------
             42
```

Now suppose you want to change the security attribute, set a configuration parameter, rename the procedure, and move it to a different schema. You must use separate `ALTER` statements for each change:

```plpgsql
alter procedure s1.p(int) security invoker;
alter procedure s1.p(int) set statement_timeout = '1s';
alter procedure s1.p(int) rename to q;
```

Create a schema to move the procedure to:

```plpgsql
create schema s2;
alter procedure s1.q(int) set schema s2;
```

You can verify the changes by querying the procedure's metadata. See the section [The "pg_proc" catalog table for subprograms](../../../user-defined-subprograms-and-anon-blocks/pg-proc-catalog-table/) for information on how to query subprogram metadata:

```plpgsql
select
  proname::text                     as name,
  pronamespace::regnamespace::text  as schema,
  case
    when prosecdef then 'definer'
    else 'invoker'
  end                               as security,
  proconfig                         as settings
from pg_proc
where
  proname::text in ('p', 'q')
order by proname;
```

This shows the updated procedure:

```output
 name | schema | security |       settings
------+--------+----------+-----------------------
 q    | s2     | invoker  | {statement_timeout=1s}
```

### Mark extension dependencies

Use `DEPENDS ON EXTENSION` to mark a procedure as dependent on an extension:

```sql
CREATE EXTENSION "uuid-ossp";

-- Mark the procedure as dependent on the extension
ALTER PROCEDURE s2.q(int) DEPENDS ON EXTENSION "uuid-ossp";

-- Remove the dependency mark if needed
ALTER PROCEDURE s2.q(int) NO DEPENDS ON EXTENSION "uuid-ossp";
```

When an extension is dropped, all dependent objects are dropped or updated accordingly.

## See also

- [`CREATE PROCEDURE`](../ddl_create_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
