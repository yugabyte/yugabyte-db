---
title: COMMENT
linkTitle: COMMENT
summary: COMMENT
description: COMMENT
menu:
  latest:
    identifier: api-ysql-commands-comment
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_comment
isTocNested: true
showAsideToc: true
---

## Synopsis

The `COMMENT` command is used to set or update the comment on an object.

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
    {{% includeMarkdown "../syntax_resources/commands/comment_on,aggregate_signature.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/comment_on,aggregate_signature.diagram.md" /%}}
  </div>
</div>

When `NULL` is passed instead of a text literal, any existing comment on the referenced object is removed.

## Examples

```
# Add comments.
COMMENT ON TABLE some_table IS 'This is a table';
COMMENT ON COLUMN some_table.id IS 'Primary key column';
COMMENT ON DATABASE postgres IS 'Default database';
COMMENT ON ACCESS METHOD lsm IS 'Default ysql index type';
COMMENT ON OPERATOR = (text, text) IS 'Text equality operator';
COMMENT ON AGGREGATE max (int) IS 'Maximum integer value';
COMMENT ON RULE pg_settings_u ON pg_settings IS 'Setting update rule';

# Remove comment.
COMMENT ON TABLE some_table IS NULL;
```

## See also

[Other YSQL Statements](..)
