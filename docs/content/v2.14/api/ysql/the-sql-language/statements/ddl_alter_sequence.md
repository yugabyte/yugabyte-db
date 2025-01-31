---
title: ALTER SEQUENCE statement [YSQL]
headerTitle: ALTER SEQUENCE
linkTitle: ALTER SEQUENCE
description: Use the ALTER SEQUENCE statement to change the definition of a sequence in the current schema.
menu:
  v2.14:
    identifier: ddl_alter_sequence
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER SEQUENCE` statement to change the definition of a sequence in the current schema.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_sequence,name,alter_sequence_options.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_sequence,name,alter_sequence_options.diagram.md" %}}
  </div>
</div>

## Semantics

### *alter_sequence*

#### ALTER SEQUENCE *sequence_name* [ IF EXISTS ]

Specify the name of the sequence (*sequence_name*). An error is raised if a sequence with that name does not exists in the current schema and `IF EXISTS` is not specified.

### *sequence_options*

#### AS *datatype*

Changes the data type of a sequence. This automatically changes the minimum and maximum values of the sequence if the previous values were beyond what the new type allows. Valid types are `smallint`, `integer`, and `bigint`.

#### INCREMENT BY *increment*

Specify the difference between consecutive values in the sequence. Default is `1`.

#### MINVALUE *minvalue* | NO MINVALUE

 Specify the minimum value allowed in the sequence. If this value is reached (in a sequence with a negative increment), `nextval()` will return an error. If `NO MINVALUE` is specified, the default value will be used. Default is 1.

#### MAXVALUE *maxvalue* | NO MAXVALUE

Specify the maximum value allowed in the sequence. If this value is reached, `nextval()` will return an error. If `NO MAXVALUE` is specified, the default will be used. Default is `2<sup>63</sup> - 1`.

#### START WITH *start*

Specify the first value in the sequence. `start` cannot be less than `minvalue`. Default is `1`.

#### RESTART [ [ WITH ] *restart* ] ]

Change the current value of the sequence. If no value is specified, the current value will be set to the last value specified with `START [ WITH ]` when the sequence was created or altered.

#### CACHE *cache*

Specify how many numbers from the sequence to cache in the client. Default is `1`.

When YB-TServer [ysql_sequence_cache_minval](../../../../../reference/configuration/yb-tserver/#ysql-sequence-cache-minval) configuration flag is not explicitly turned off (set to `0`), the maximum value of the flag and the cache clause will be used.

#### OWNED BY *table_name.table_column* | NONE

It gives ownership of the sequence to the specified column (if any). This means that if the column (or the table to which it belongs to) is dropped, the sequence will be automatically dropped. If `NONE` is specified, any previous ownership will be deleted.

## Examples

Create a simple sequence.

```plpgsql
yugabyte=# CREATE SEQUENCE s;
```

```
CEATE SEQUENCE
```

Modify the increment value.

```plpgsql
yugabyte=# ALTER SEQUENCE s INCREMENT BY 5;
```

```
ALTER SEQUENCE
```

```plpgsql
yugabyte=# SELECT nextval('s');
```

```
 nextval
---------
       1
(1 row)
```

```plpgsql
yugabyte=# SELECT nextval('s');
```

```
 nextval
---------
       6
(1 row)
```

Modify the starting value.

```plpgsql
yugabyte=# ALTER SEQUENCE s RESTART WITH 2;
```

```
ALTER SEQUENCE
```

```plpgsql
yugabyte=# SELECT nextval('s');
```

```
 nextval
---------
       2
(1 row)
```

```plpgsql
yugabyte=# SELECT nextval('s');
```

```
 nextval
---------
       7
(1 row)
```

## See also

- [`CREATE SEQUENCE`](../ddl_create_sequence)
- [`DROP SEQUENCE`](../ddl_drop_sequence)
- [`currval()`](../../../exprs/func_currval)
- [`lastval()`](../../../exprs/func_lastval)
- [`nextval()`](../../../exprs/func_nextval)
