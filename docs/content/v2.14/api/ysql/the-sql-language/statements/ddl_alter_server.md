---
title: ALTER SERVER statement [YSQL]
headerTitle: ALTER SERVER
linkTitle: ALTER SERVER
description: Use the ALTER SERVER statement to create alter a foreign server.
menu:
  v2.14:
    identifier: ddl_alter_server
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER SERVER` command to alter the definition of a foreign server. The command can be used to alter the options of the foreign server, or to change its owner.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_server.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_server.diagram.md" %}}
  </div>
</div>

## Semantics

Alter the foreign server named **server_name**.

### Version
The `VERSION` clause can be used to specify the updated version of the server.

### Options
The `OPTIONS` clause can be used to specify the new options of the foreign server. `ADD`, `SET`, and `DROP` specify the action to be performed. `ADD` is assumed if no operation is explicitly specified.

## Examples

Change the server's version to `2.0`, set `opt1` to `'true'`, and drop `opt2`.
```plpgsql
yugabyte=# ALTER SERVER my_server SERVER VERSION '2.0' OPTIONS (SET opt1 'true', DROP opt2);
```
## See also

- [`CREATE SERVER`](../ddl_create_server/)
- [`DROP SERVER`](../ddl_drop_server/)
