---
title: CREATE PROCEDURE statement [YSQL]
headerTitle: CREATE PROCEDURE
linkTitle: CREATE PROCEDURE
description: Use the CREATE PROCEDURE statement to create a procedure in a database.
menu:
  v2.16:
    identifier: ddl_create_procedure
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE PROCEDURE` statement to create a procedure in a database.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_procedure,arg_decl_with_dflt,arg_decl,subprogram_signature,unalterable_proc_attribute,lang_name,implementation_definition,sql_stmt_list,alterable_fn_and_proc_attribute.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_procedure,arg_decl_with_dflt,arg_decl,subprogram_signature,unalterable_proc_attribute,lang_name,implementation_definition,sql_stmt_list,alterable_fn_and_proc_attribute.diagram.md" %}}
  </div>
</div>

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

{{< tip title="'Create procedure' grants 'execute' to 'public'." >}}
_Execute_ is granted automatically to _public_ when you create a new procedure. This is very unlikely to be want you want—and so this behavior presents a disguised security risk. Yugabyte recommends that your standard practice be to revoke this privilege immediately after creating a procedure.
{{< /tip >}}

{{< tip title="You cannot set the 'depends on extension' attribute with 'create procedure'." >}}
A procedure's _depends on extension_ attribute cannot be set using `CREATE [OR REPLACE] PROCEDURE`. You must use [`ALTER PROCEDURE`](../ddl_alter_procedure) to set it.
{{< /tip >}}

## Examples

- Set up an accounts table.
    ```plpgsql
    CREATE TABLE accounts (
      id integer PRIMARY KEY,
      name text NOT NULL,
      balance decimal(15,2) NOT NULL
    );

    INSERT INTO accounts VALUES (1, 'Jane', 100.00);
    INSERT INTO accounts VALUES (2, 'John', 50.00);

    SELECT * from accounts;
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
  CREATE OR REPLACE PROCEDURE transfer(integer, integer, decimal)
  LANGUAGE plpgsql
  AS $$
  BEGIN
    IF $3 <= 0.00 then RAISE EXCEPTION 'Can only transfer positive amounts'; END IF;
    IF $1 = $2 then RAISE EXCEPTION 'Sender and receiver cannot be the same'; END IF;
    UPDATE accounts SET balance = balance - $3 WHERE id = $1;
    UPDATE accounts SET balance = balance + $3 WHERE id = $2;
    COMMIT;
  END;
  $$;
  ```

- Transfer $20.00 from Jane to John.

  ```plpgsql
  CALL transfer(1, 2, 20.00);
  SELECT * from accounts;
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
  CALL transfer(2, 2, 20.00);
  ```

  ```output
  ERROR:  Sender and receiver cannot be the same
  CONTEXT:  PL/pgSQL function transfer(integer,integer,numeric) line 4 at RAISE
  ```

  ```plpgsql
  yugabyte=# CALL transfer(1, 2, -20.00);
  ```

  ```output
  ERROR:  Can only transfer positive amounts
  CONTEXT:  PL/pgSQL function transfer(integer,integer,numeric) line 3 at RAISE
  ```

## See also

- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
