---
title: ALTER SEQUENCE statement [YSQL]
headerTitle: ALTER SEQUENCE
linkTitle: ALTER SEQUENCE
description: Use the ALTER SEQUENCE statement to change the definition of a sequence in the current schema.
menu:
  v2.20:
    identifier: ddl_alter_sequence
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER SEQUENCE` statement to change the definition of a sequence in the current schema.

## Syntax

{{%ebnf%}}
  alter_sequence,
  alter_sequence_options
{{%/ebnf%}}

## Semantics

### *alter_sequence*

#### ALTER SEQUENCE *sequence_name* [ IF EXISTS ]

Specify the name of the sequence (*sequence_name*). An error is raised if a sequence with that name does not exists in the current schema and `IF EXISTS` is not specified.

### *sequence_options*

#### AS *seq_data_type*

Changes the data type of a sequence. This automatically changes the minimum and maximum values of the sequence if the previous values were beyond what the new type allows. Valid types are `smallint`, `integer`, and `bigint`.

#### INCREMENT BY *int_literal*

Specify the difference between consecutive values in the sequence. Default is `1`.

#### MINVALUE *int_literal* | NO MINVALUE

 Specify the minimum value allowed in the sequence. If this value is reached (in a sequence with a negative increment), `nextval()` will return an error. If `NO MINVALUE` is specified, the default value will be used. Default is 1.

#### MAXVALUE *int_literal* | NO MAXVALUE

Specify the maximum value allowed in the sequence. If this value is reached, `nextval()` will return an error. If `NO MAXVALUE` is specified, the default will be used. Default is `2⁶³-1`.

#### START WITH *int_literal*

Specify the first value in the sequence. `start` cannot be less than `minvalue`. Default is `1`.

#### RESTART [ [ WITH ] *int_literal* ] ]

Change the current value of the sequence. If no value is specified, the current value will be set to the last value specified with `START [ WITH ]` when the sequence was created or altered.

#### CACHE *int_literal*

Specify how many numbers from the sequence to cache in the client. Default is `1`.

When YB-TServer [ysql_sequence_cache_minval](../../../../../reference/configuration/yb-tserver/#ysql-sequence-cache-minval) configuration flag is not explicitly turned off (set to `0`), the maximum value of the flag and the cache clause will be used.

#### [ NO ] CYCLE

If `CYCLE` is specified, the sequence will wrap around once it has reached `minvalue` or `maxvalue`. If `maxvalue` was reached, `minvalue` will be the next number in the sequence. If `minvalue` was reached (for a descending sequence), `maxvalue` will be the next number in a sequence. `NO CYCLE` is the default.

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
