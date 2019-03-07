---
title: CREATE SEQUENCE
summary: Create a sequence in the current schema
description: CREATE SEQUENCE
menu:
  latest:
    identifier: api-postgresql-create-sequence
    parent: api-postgresql-sequences
aliases:
  - /latest/api/postgresql/create_sequence
  - /latest/api/ysql/create_sequence
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE SEQUENCE` statement creates a new sequence in the current schema.

## Syntax

### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="650" height="50" viewbox="0 0 650 50"><path class="connector" d="M0 22h5m67 0h10m87 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m118 0h10m127 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="87" height="25" rx="7"/><text class="text" x="92" y="22">SEQUENCE</text><rect class="literal" x="199" y="5" width="32" height="25" rx="7"/><text class="text" x="209" y="22">IF</text><rect class="literal" x="241" y="5" width="45" height="25" rx="7"/><text class="text" x="251" y="22">NOT</text><rect class="literal" x="296" y="5" width="64" height="25" rx="7"/><text class="text" x="306" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="390" y="5" width="118" height="25"/><text class="text" x="400" y="22">sequence_name</text></a><a xlink:href="../grammar_diagrams#sequence-options"><rect class="rule" x="518" y="5" width="127" height="25"/><text class="text" x="528" y="22">sequence_options</text></a></svg>

### sequence_name
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### sequence_options
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1205" height="70" viewbox="0 0 1205 70"><path class="connector" d="M0 22h25m92 0h30m35 0h20m-70 0q5 0 5 5v8q0 5 5 5h45q5 0 5-5v-8q0-5 5-5m5 0h10m81 0h20m-303 0q5 0 5 5v23q0 5 5 5h278q5 0 5-5v-23q0-5 5-5m5 0h30m84 0h10m75 0h20m-199 25q0 5 5 5h5m38 0h10m84 0h42q5 0 5-5m-194-25q5 0 5 5v33q0 5 5 5h179q5 0 5-5v-33q0-5 5-5m5 0h30m86 0h10m78 0h20m-204 25q0 5 5 5h5m38 0h10m86 0h45q5 0 5-5m-199-25q5 0 5 5v33q0 5 5 5h184q5 0 5-5v-33q0-5 5-5m5 0h30m58 0h30m53 0h20m-88 0q5 0 5 5v8q0 5 5 5h63q5 0 5-5v-8q0-5 5-5m5 0h10m48 0h20m-254 0q5 0 5 5v23q0 5 5 5h229q5 0 5-5v-23q0-5 5-5m5 0h30m61 0h10m54 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="92" height="25" rx="7"/><text class="text" x="35" y="22">INCREMENT</text><rect class="literal" x="147" y="5" width="35" height="25" rx="7"/><text class="text" x="157" y="22">BY</text><a xlink:href="../grammar_diagrams#increment"><rect class="rule" x="212" y="5" width="81" height="25"/><text class="text" x="222" y="22">increment</text></a><rect class="literal" x="343" y="5" width="84" height="25" rx="7"/><text class="text" x="353" y="22">MINVALUE</text><a xlink:href="../grammar_diagrams#minvalue"><rect class="rule" x="437" y="5" width="75" height="25"/><text class="text" x="447" y="22">minvalue</text></a><rect class="literal" x="343" y="35" width="38" height="25" rx="7"/><text class="text" x="353" y="52">NO</text><rect class="literal" x="391" y="35" width="84" height="25" rx="7"/><text class="text" x="401" y="52">MINVALUE</text><rect class="literal" x="562" y="5" width="86" height="25" rx="7"/><text class="text" x="572" y="22">MAXVALUE</text><a xlink:href="../grammar_diagrams#maxvalue"><rect class="rule" x="658" y="5" width="78" height="25"/><text class="text" x="668" y="22">maxvalue</text></a><rect class="literal" x="562" y="35" width="38" height="25" rx="7"/><text class="text" x="572" y="52">NO</text><rect class="literal" x="610" y="35" width="86" height="25" rx="7"/><text class="text" x="620" y="52">MAXVALUE</text><rect class="literal" x="786" y="5" width="58" height="25" rx="7"/><text class="text" x="796" y="22">START</text><rect class="literal" x="874" y="5" width="53" height="25" rx="7"/><text class="text" x="884" y="22">WITH</text><a xlink:href="../grammar_diagrams#start"><rect class="rule" x="957" y="5" width="48" height="25"/><text class="text" x="967" y="22">start</text></a><rect class="literal" x="1055" y="5" width="61" height="25" rx="7"/><text class="text" x="1065" y="22">CACHE</text><a xlink:href="../grammar_diagrams#cache"><rect class="rule" x="1126" y="5" width="54" height="25"/><text class="text" x="1136" y="22">cache</text></a></svg>

### increment
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### minvalue
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### maxvalue
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### start
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### cache
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>


### Grammar
```
create_sequence ::= CREATE SEQUENCE [ IF NOT EXISTS ] sequence_name sequence_options;
sequence_name ::= '<Text Literal>';
sequence_options ::= [ INCREMENT [ BY ] increment ]
                     [ MINVALUE minvalue | NO MINVALUE ]
                     [ MAXVALUE maxvalue | NO MAXVALUE ]
                     [ START [ WITH ] start ] [ CACHE cache ];
increment ::= '<Integer Literal>';
minvalue ::= '<Integer Literal>';
maxvalue ::= '<Integer Literal>';
start ::= '<Integer Literal>';
cache ::= '<Integer Literal>';
```

Where

- `sequence_name` is the name of the sequence.
- `increment` is the difference between consecutive values in the sequence. Default is 1.
- `minvalue` is the minimum value allowed in the sequence. If this value is reached (in a sequence with a negative increment), `nextval()` will return an error. If `NO MINVALUE` is specified, the default value will be used. Default is 1.
- `maxvalue` is the maximum value allowed in the sequence. If this value is reached, `nextval()` will return an error. If `NO MAXVALUE` is specified, the default will be used. Default is 2^63 - 1.
- `start` is the first value in the sequence. `start` cannot be less than `minvalue`. Default is 1.
- `cache` specifies how many numbers from the sequence to cache in the client. Default is 1.

## Semantics
- An error is raised if a sequence with that name already exists in the current schema and `IF NOT EXISTS` is not specified.

## Cache
In YSQL as in Postgres, the sequence's data is stored in a persistent system table. In YSQL this table has one row per sequence and it stores the sequence data in two values:

- `last_val` stores the last value used or the next value to be used.
- `is_called` stores whether `last_val` has been used. If false, `last_val` is the next value in the sequence. Otherwise, `last_val` + `INCREMENT` is the next one.

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

## See Also
[`DROP SEQUENCE`](../drop_sequence)
[`currval()`](../currval_sequence)
[`lastval()`](../lastval_sequence)
[`nextval()`](../nextval_sequence)
[Other PostgreSQL Statements](..)
