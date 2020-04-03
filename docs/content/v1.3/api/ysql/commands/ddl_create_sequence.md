---
title: CREATE SEQUENCE
linkTitle: CREATE SEQUENCE
summary: Create a sequence in the current schema
description: CREATE SEQUENCE
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-create-sequence
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `CREATE SEQUENCE` statement to create a new sequence in the current schema.

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
    {{% includeMarkdown "../syntax_resources/commands/create_sequence,sequence_name,sequence_options.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_sequence,sequence_name,sequence_options.diagram.md" /%}}
  </div>
</div>

## Semantics

### *create_sequence*

#### CREATE SEQUENCE *sequence_name* [ IF NOT EXISTS ]

Specify the name of the sequence (*sequence_name*). An error is raised if a sequence with that name already exists in the current schema and `IF NOT EXISTS` is not specified.

### *sequence_options*

#### INCREMENT BY *increment*

Specify the difference between consecutive values in the sequence. Default is `1`.

#### MINVALUE *minvalue* | NO MINVALUE

 Specify the minimum value allowed in the sequence. If this value is reached (in a sequence with a negative increment), `nextval()` will return an error. If `NO MINVALUE` is specified, the default value will be used. Default is 1.

### MAXVALUE *maxvalue* | NO MAXVALUE

Specify the maximum value allowed in the sequence. If this value is reached, `nextval()` will return an error. If `NO MAXVALUE` is specified, the default will be used. Default is `2<sup>63</sup> - 1`.

### START WITH *start*

Specify the first value in the sequence. `start` cannot be less than `minvalue`. Default is `1`.

### CACHE *cache*

Specify how many numbers from the sequence to cache in the client. Default is `1`.

## Cache

In YSQL as in PostgreSQL, the sequence's data is stored in a persistent system table. In YSQL this table has one row per sequence and it stores the sequence data in two values:

### *last_val*

Stores the last value used or the next value to be used.

### *is_called*

Stores whether `last_val` has been used. If false, `last_val` is the next value in the sequence. Otherwise, `last_val` + `INCREMENT` is the next one.

By default (when `INCREMENT` is 1), each call to `nextval()` updates `last_val` for that sequence. In YSQL, the table holding the sequence's data is replicated as opposed to being in the local file system. Each update to this table requires two RPCs (and will be optimized to one RPC in the future), In any case, the latency experienced by a call to `nextval()` in YSQL will be significantly higher than the same operation in Postgres. To avoid such performance degradation, we recommend using a cache value with a value large enough. Cached values are stored in the memory of the local node, and retrieving such values avoids any RPCs, so the latency of one cache allocation can be amortized over all the numbers allocated for the cache.

`SERIAL` types create a sequence with a cache with default value of 1. So `SERIAL` types should be avoided, and their equivalent statement should be used.
Instead of creating a table with a `SERIAL` type like this:

```
CREATE TABLE t(k SERIAL)
```

You should create a sequence with a large enough cache first, and then set the column that you want to have a serial type to `DEFAULT` to `nextval()` of the sequence.

```
CREATE SEQUENCE t_k_seq CACHE 10000;
CREATE TABLE t(k integer NOT NULL DEFAULT nextval('t_k_seq'));
```

## Examples

Create a simple sequence that increments by 1 every time `nextval()` is called.

```sql
postgres=# CREATE SEQUENCE s;
```

```
CREATE SEQUENCE
```

Call `nextval()`.

```sql
postgres=# SELECT nextval('s');
```

```
 nextval
---------
       1
(1 row)
```

Create a sequence with a cache of 10,000 values.

```sql
postgres=# CREATE SEQUENCE s2 CACHE 10000;
```

```
CREATE SEQUENCE
```

In the same session, select `nextval()`.

```sql
SELECT nextval('s2');
```

```
 nextval
---------
       1
(1 row)
```

In a different session, select `nextval()`.

```sql
SELECT nextval('s2');
```

```
nextval
---------
   10001
(1 row)
```

Create a sequence that starts at 0. MINVALUE also has to be changed from its default 1 to something less than or equal to 0.

```sql
CREATE SEQUENCE s3 START 0 MINVALUE 0;
```

```
CREATE SEQUENCE
```

```sql
SELECT nextval('s3');
```

```
nextval
---------
       0
(1 row)
```

## See also

- [`DROP SEQUENCE`](../drop_sequence)
- [`currval()`](../currval_sequence)
- [`lastval()`](../lastval_sequence)
- [`nextval()`](../nextval_sequence)
