---
title: CREATE AGGREGATE
linkTitle: CREATE AGGREGATE
summary: Create a new type in a database
description: CREATE AGGREGATE
menu:
  latest:
    identifier: api-ysql-commands-create-aggregate
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_aggregate/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `CREATE AGGREGATE` statement to create a new aggregate function.  There are three ways to
create aggregates.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_aggregate,create_aggregate_normal,create_aggregate_order_by,create_aggregate_old,aggregate_arg,aggregate_normal_option,aggregate_order_by_option,aggregate_old_option.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_aggregate,create_aggregate_normal,create_aggregate_order_by,create_aggregate_old,aggregate_arg,aggregate_normal_option,aggregate_order_by_option,aggregate_old_option.diagram.md" /%}}
  </div>
</div>

## Semantics

The order of options does not matter.  Even the mandatory options `BASETYPE`, `SFUNC`, and `STYPE`
may appear in any order.

- TODO

## Examples

TODO

## See also

- [`DROP AGGREGATE`](../ddl_drop_aggregate)
