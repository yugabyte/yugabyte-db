---
title: CREATE PROCEDURE statement [YSQL]
headerTitle: CREATE PROCEDURE
linkTitle: CREATE PROCEDURE
description: Use the CREATE PROCEDURE statement to create a procedure in a database.
menu:
  preview:
    identifier: ddl_create_procedure
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_create_procedure/
type: docs
---

{{< tip title="See the dedicated 'User-defined subprograms and anonymous blocks' section." >}}
User-defined procedures are part of a larger area of functionality. See this major section:

- [User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"](../../../user-defined-subprograms-and-anon-blocks/) 
{{< /tip >}}

## Synopsis

Use the `CREATE PROCEDURE` statement to create a procedure in a database.

## Syntax

{{%ebnf%}}
  create_procedure,
  arg_decl_with_dflt,
  arg_decl,
  subprogram_signature,
  unalterable_proc_attribute,
  lang_name,
  subprogram_implementation,
  sql_stmt_list,
  alterable_fn_and_proc_attribute
{{%/ebnf%}}

{{< tip title="'create procedure' and the 'subprogram_signature' rule." >}}
When you write a `CREATE PROCEDURE` statement, you will already have decided what formal arguments it will have—i.e. for each, what will be its name, mode, data type, and optionally its default value. When, later, you alter or drop a procedure, you must identify it. You do this, in  [`ALTER PROCEDURE`](../ddl_alter_procedure/) and [`DROP PROCEDURE`](../ddl_drop_procedure/), typically by specifying just its _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_. You are allowed to use the full _subprogram_signature_. But this is unconventional. Notice that the _subprogram_signature_ does not include the optional specification of default values; and you _cannot_ mention these when you alter or drop a procedure. The distinction between the _subprogram_signature_ and the _subprogram_call_signature_ is discussed carefully in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).
{{< /tip >}}


## Semantics

- The meanings of the various procedure attributes are explained in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).

- A procedure, like other schema objects such as a table, inevitably has an owner. You cannot specify the owner explicitly when a procedure is created. Rather, it's defined implicitly as what the _current_user_ built-in function returns when it's invoked in the session that creates the procedure. This user must have the _usage_ privilege on the procedure's schema and its argument data types. You (optionally) specify the procedure's schema and (mandatorily) its name within its schema as the argument of the _[subprogram_name](../../../syntax_resources/grammar_diagrams/#subprogram-name)_ rule.

- If a procedure with the given name, schema, and argument types already exists then `CREATE PROCEDURE` will draw an error unless the `CREATE OR REPLACE PROCEDURE` variant is used.

- Procedures with different _[subprogram_call_signatures](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ can share the same _[subprogram_name](../../../syntax_resources/grammar_diagrams/#subprogram-name)_. (The same holds for functions.) See section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

- `CREATE OR REPLACE PROCEDURE` doesn't change permissions that have been granted on an existing procedure. To use this statement, the current user must own the procedure, or be a member of the role that owns it.

- In contrast, if you drop and then recreate a procedure, the new procedure is not the same entity as the old one. So you will have to drop existing objects that depend upon the old procedure. (Dropping the procedure `CASCADE` achieves this.) Alternatively, `ALTER PROCEDURE` can be used to change most of the auxiliary attributes of an existing procedure.
- The languages supported by default are `SQL`, `PLPGSQL` and `C`.

<a name="create-procedure-grants-execute-to-public"></a>
{{< tip title="'Create procedure' grants 'execute' to 'public'." >}}
_Execute_ is granted automatically to _public_ when you create a new procedure. This is very unlikely to be want you want—and so this behavior presents a disguised security risk. Yugabyte recommends that your standard practice be to revoke this privilege immediately after creating a procedure.
{{< /tip >}}

{{< tip title="You cannot set the 'depends on extension' attribute with 'create procedure'." >}}
A procedure's _depends on extension_ attribute cannot be set using `CREATE [OR REPLACE] PROCEDURE`. You must use [`ALTER PROCEDURE`](../ddl_alter_procedure) to set it.
{{< /tip >}}

## Examples

- Set up an accounts table.
    ```plpgsql
    create schema s;
    
    create table s.accounts (
      id integer primary key,
      name text not null,
      balance decimal(15,2) not null);
    
    insert into s.accounts values (1, 'Jane', 100.00);
    insert into s.accounts values (2, 'John', 50.00);
    
    select * from s.accounts order by 1;
    ```
    
    ```output
     id | name | balance
    ----+------+---------
      1 | Jane |  100.00
      2 | John |   50.00
    (2 rows)
    ```
- Define a _transfer_ procedure to transfer money from one account to another.

  ```plpgsql
  create or replace procedure s.transfer(from_id in int, to_id in int, amnt in decimal)
    security definer
    set search_path = pg_catalog, pg_temp
    language plpgsql
  as $body$
  begin
    if amnt <= 0.00 then
      raise exception 'The transfer amount must be positive';
    end if;
    if from_id = to_id then
      raise exception 'Sender and receiver cannot be the same';
    end if;
    update s.accounts set balance = balance - amnt where id = from_id;
    update s.accounts set balance = balance + amnt where id = to_id;
  end;
  $body$;
  ```

- Transfer $20.00 from Jane to John.

  ```plpgsql
  call s.transfer(1, 2, 20.00);
  select * from s.accounts order by 1;
  ```

  ```output
   id | name | balance
  ----+------+---------
    1 | Jane |   80.00
    2 | John |   70.00
  (2 rows)
  ```

- Errors will be thrown for unsupported argument values.

  ```plpgsql
  CALL s.transfer(2, 2, 20.00);
  ```

  ```output
  ERROR:  Sender and receiver cannot be the same
  ```
  
  ```plpgsql
  call s.transfer(1, 2, -20.00);
  ```
  
  ```output
  ERROR:  The transfer amount must be positive
  ```

{{< tip title="Always name the formal arguments and write the procedure's body last." >}}
YSQL inherits from PostgreSQL the ability to specify the arguments only by listing their data types and to reference them in the body using the positional notation `$1`, `$2`, and so on. The earliest versions of PostgreSQL didn't allow named parameters. But the version that YSQL is based on does allow this. Your code will be very much easier to understand if you use named arguments like the example does.


The syntax rules allow you to write the alterable and unalterable attributes in any order, like this:

```plpgsql


create or replace procedure s.transfer(from_id in int, to_id in int, amnt in decimal)
security definer
as $body$
begin
  if amnt <= 0.00 then
    raise exception 'The transfer amount must be positive';
  end if;
  if from_id = to_id then
    raise exception 'Sender and receiver cannot be the same';
  end if;
  update s.accounts set balance = balance - amnt where id = from_id;
  update s.accounts set balance = balance + amnt where id = to_id;
end;
$body$
set search_path = pg_catalog, pg_temp
language plpgsql;
```

Yugabyte recommends that you avoid exploiting this freedom and choose a standard order where, especially, you write the body last.  (Try the \\_sf_ meta-command for a procedure that you created. It always shows the source text last, no matter what order your _create [or replace]_ used.) For example, it helps readability to specify the language immediately before the body. Following this practice will allow you to review, and discuss, your code in a natural way by distinguishing, informally, between the procedure _header_ (i.e. everything that comes before the body) and the _implementation_ (i.e. the body).
{{< /tip >}}


## See also

- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
