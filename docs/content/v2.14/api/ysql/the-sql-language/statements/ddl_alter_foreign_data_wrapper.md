---
title: ALTER FOREIGN DATA WRAPPER statement [YSQL]
headerTitle: ALTER FOREIGN DATA WRAPPER
linkTitle: ALTER FOREIGN DATA WRAPPER
description: Use the ALTER FOREIGN DATA WRAPPER statement to alter a foreign-data wrapper.
menu:
  v2.14:
    identifier: ddl_alter_foreign_data_wrapper
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER FOREIGN DATA WRAPPER` command to alter the definition of the foreign-data wrapper. This command can be used to alter the handler/validator functions or the options of the foreign-data wrapper. It can also be used to change the owner or rename the foreign-data wrapper.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_foreign_data_wrapper.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_foreign_data_wrapper.diagram.md" %}}
  </div>
</div>

## Semantics

Alter the foreign-data wrapper named **fdw_name**.

### Handler:
The `HANDLER` clause can be used to specify the handler function.
The `NO HANDLER` clause can be used to specify that the foreign-data wrapper has no handler function.

### Validator
The `VALIDATOR` clause can be used to specify the validator function.
The `NO VALIDATOR` can be used to specify that the foreign-data wrapper has no validator function.

### Options
The `OPTIONS` clause can be used to specify the new options of the foreign-data wrapper. `ADD`, `SET`, and `DROP` specify the action to be performed. `ADD` is assumed if no operation is explicitly specified.

The new owner of the FDW can be specified using **new_owner**
The new name of the FDW can be specified using **new_name**

## Examples

Change the handler to `newhandler`.

```plpgsql
yugabyte=# ALTER FOREIGN DATA WRAPPER my_wrapper HANDLER;
```

Alter the foreign-data wrapper to have no validator.

```plpgsql
yugabyte=# ALTER FOREIGN DATA WRAPPER my_wrapper NO VALIDATOR;
```

Alter the foreign-data wrapper's options: add `new` and set it to `'1'`, change the value of `old` to `'2'`.

```plpgsql
yugabyte=# ALTER FOREIGN DATA WRAPPER my_wrapper OPTIONS(ADD new '1', SET old '2');
```

## See also

- [`CREATE FOREIGN DATA WRAPEPR`](../ddl_create_foreign_data_wrapper/)
- [`DROP FOREIGN DATA WRAPPER`](../ddl_create_foreign_data_wrapper/)
