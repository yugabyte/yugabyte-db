---
title: DROP PROCEDURE statement [YSQL]
headerTitle: DROP PROCEDURE
linkTitle: DROP PROCEDURE
description: Use the DROP PROCEDURE statement to remove a procedure from a database.
menu:
  v2.14:
    identifier: ddl_drop_procedure
    parent: statements
type: docs
---

## Synopsis

Use the `DROP PROCEDURE` statement to remove a procedure from a database.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_procedure,subprogram_signature,arg_decl.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_procedure,subprogram_signature,arg_decl.diagram.md" %}}
  </div>
</div>

You must identify the to-be-dropped procedure by:

- Its name and the schema where it lives. This can be done by using its fully qualified name or by using just its bare name and letting name resolution find it in the first schema on the _search_path_ where it occurs. Notice that you don't need to (and cannot) mention the name of its owner.

- Its signature. The _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ is sufficient; and this is typically used. You can use the full _subprogram_signature_. But you should realize that the _arg_name_ and _arg_mode_ for each _arg_decl_ carry no identifying information. (This is why it is not typically used when a function or procedure is to be altered or dropped.) This is explained in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

## Semantics

- An error will be thrown if the procedure does not exist unless `IF EXISTS` is used. Then a notice is issued instead.

- `RESTRICT` is the default and it will not drop the procedure if any objects depend on it.

- `CASCADE` will drop any objects that transitively depend on the procedure.

## Examples

```plpgsql
DROP PROCEDURE IF EXISTS transfer(integer, integer, dec) CASCADE;
```

## See also

- [`CREATE PROCEDURE`](../ddl_create_procedure)
- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
