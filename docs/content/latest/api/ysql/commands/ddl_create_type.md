---
title: CREATE TYPE
linkTitle: CREATE TYPE
summary: Create a new type in a database
description: CREATE TYPE
menu:
  latest:
    identifier: api-ysql-commands-create-type
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_type
isTocNested: true
showAsideToc: true
---

## Synopsis

The `CREATE TYPE` command creates a new type in a database.

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
    {{% includeMarkdown "../syntax_resources/commands/create_composite_type,create_enum_type,create_range_type,create_base_type,create_shell_type,composite_type_elem,range_type_option,base_type_option.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_composite_type,create_enum_type,create_range_type,create_base_type,create_shell_type,composite_type_elem,range_type_option,base_type_option.diagram.md" /%}}
  </div>
</div>

Where

- `type_name` specifies the name of this user-defined type.
- `attribute_name` specifies the name of an attribute for this composite type.
- `data_type` specifies the type of an attribute for this composite type.
- `collation` specifies the collation to use for this type.  In case this is a composite type, the
  attribute data type must be collatable.  In case this is a range type, the subtype must be
  collatable.
- `label` specifies a quoted label to be a vaule of this enumerated type.
- `subtype` specifies the type to use for this range type.
- `subtype_operator_class` specifies the operator class to use for the subtype of this range type.
- `canonical_function` specifies the canonical function used when converting range values of this
  range type to a canonical form.
- `subtype_diff_function` specifies the subtype difference function used to take the difference
  between two range values of this range type.
- `input_function` specifies the function that converts this type's external textual representation
  to internal representation.
- `output_function` specifies the function that converts this type's internal representation to
  external textual representation.
- `receive_function` specifies the function that converts this type's external binary representation
  to internal representation.
- `send_function` specifies the function that converts this type's internal representation to
  external binary representation.
- `type_modifier_input_function` specifies the function that converts this type modifier's external
  textual representation to internal integer typmod value or throws an error.
- `type_modifier_output_function` specifies the function that converts this type modifier's internal
  integer typmod value to external representation.
- `internallength` specifies the size in bytes of this type.
- `alignment` specifies the storage alignment of this type.
- `storage` specifies the storage strategy of this type.  This type must be variable length.
- `like_type` specifies the type to copy over the `INTERNALLENGTH`, `PASSEDBYVALUE`, `ALIGNMENT`,
  and `STORAGE` values from.
- `category` specifies the category code for this type.
- `preferred` specifies whether this type is preferred for implicit casts in the same category.
- `default` specifies the default value of this type.
- `element` specifies the elements this type, also making this type an array.
- `delimiter` specifies the character used to separate array elements in the external textual
  representation of values of this type.
- `collatable` specifies whether collation information may be passed to operations that use this
  type.

## Examples

Composite type

```sql
postgres=# CREATE TYPE feature_struct AS (id INTEGER, name TEXT);
postgres=# CREATE TABLE feature_tab_struct (feature_col feature_struct);
```

Enumerated type

```sql
postgres=# CREATE TYPE feature_enum AS ENUM('one', 'two', 'three');
postgres=# CREATE TABLE feature_tab_enum (feature_col feature_enum);
```

## See also

- [Other YSQL Statements](..)
