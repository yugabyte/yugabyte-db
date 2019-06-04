---
title: INSERT
summary: Add new rows to a table
description: INSERT
menu:
  latest:
    identifier: api-ysql-commands-insert
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/dml_insert/
  - /latest/api/postgresql/dml_insert/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `INSERT` command adds one or more rows to the specified table.

## Syntax

### Diagrams

#### insert

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="612" height="385" viewbox="0 0 612 385"><path class="connector" d="M0 52h25m53 0h30m90 0h20m-125 0q5 0 5 5v8q0 5 5 5h100q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h37q5 0 5 5v20q0 5-5 5m-5 0h40m-366 0q5 0 5 5v23q0 5 5 5h341q5 0 5-5v-23q0-5 5-5m5 0h5m-381 65h5m65 0h10m50 0h10m91 0h30m36 0h10m46 0h20m-127 0q5 0 5 5v8q0 5 5 5h102q5 0 5-5v-8q0-5 5-5m5 0h30m25 0h10m112 0h10m25 0h20m-217 0q5 0 5 5v8q0 5 5 5h192q5 0 5-5v-8q0-5 5-5m5 0h5m-610 50h25m75 0h10m68 0h429m-592 40q0 5 5 5h5m68 0h10m25 0h10m110 0h10m25 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h224q5 0 5 5v17q0 5-5 5m-195 0h10m25 0h10m110 0h10m25 0h40m-289 0q5 0 5 5v8q0 5 5 5h264q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-587-40q5 0 5 5v80q0 5 5 5h5m75 0h492q5 0 5-5v-80q0-5 5-5m5 0h5m-612 125h25m38 0h10m80 0h30m106 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h20m-435 0q5 0 5 5v23q0 5 5 5h410q5 0 5-5v-23q0-5 5-5m5 0h5m-450 65h25m121 0h20m-156 0q5 0 5 5v8q0 5 5 5h131q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="35" width="53" height="25" rx="7"/><text class="text" x="35" y="52">WITH</text><rect class="literal" x="108" y="35" width="90" height="25" rx="7"/><text class="text" x="118" y="52">RECURSIVE</text><rect class="literal" x="280" y="5" width="24" height="25" rx="7"/><text class="text" x="290" y="22">,</text><a xlink:href="../../grammar_diagrams#with-query"><rect class="rule" x="248" y="35" width="88" height="25"/><text class="text" x="258" y="52">with_query</text></a><rect class="literal" x="5" y="100" width="65" height="25" rx="7"/><text class="text" x="15" y="117">INSERT</text><rect class="literal" x="80" y="100" width="50" height="25" rx="7"/><text class="text" x="90" y="117">INTO</text><a xlink:href="../../grammar_diagrams#table-name"><rect class="rule" x="140" y="100" width="91" height="25"/><text class="text" x="150" y="117">table_name</text></a><rect class="literal" x="261" y="100" width="36" height="25" rx="7"/><text class="text" x="271" y="117">AS</text><a xlink:href="../../grammar_diagrams#alias"><rect class="rule" x="307" y="100" width="46" height="25"/><text class="text" x="317" y="117">alias</text></a><rect class="literal" x="403" y="100" width="25" height="25" rx="7"/><text class="text" x="413" y="117">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="438" y="100" width="112" height="25"/><text class="text" x="448" y="117">column_names</text></a><rect class="literal" x="560" y="100" width="25" height="25" rx="7"/><text class="text" x="570" y="117">)</text><rect class="literal" x="25" y="150" width="75" height="25" rx="7"/><text class="text" x="35" y="167">DEFAULT</text><rect class="literal" x="110" y="150" width="68" height="25" rx="7"/><text class="text" x="120" y="167">VALUES</text><rect class="literal" x="25" y="195" width="68" height="25" rx="7"/><text class="text" x="35" y="212">VALUES</text><rect class="literal" x="103" y="195" width="25" height="25" rx="7"/><text class="text" x="113" y="212">(</text><a xlink:href="../../grammar_diagrams#column-values"><rect class="rule" x="138" y="195" width="110" height="25"/><text class="text" x="148" y="212">column_values</text></a><rect class="literal" x="258" y="195" width="25" height="25" rx="7"/><text class="text" x="268" y="212">)</text><rect class="literal" x="333" y="195" width="24" height="25" rx="7"/><text class="text" x="343" y="212">,</text><rect class="literal" x="367" y="195" width="25" height="25" rx="7"/><text class="text" x="377" y="212">(</text><a xlink:href="../../grammar_diagrams#column-values"><rect class="rule" x="402" y="195" width="110" height="25"/><text class="text" x="412" y="212">column_values</text></a><rect class="literal" x="522" y="195" width="25" height="25" rx="7"/><text class="text" x="532" y="212">)</text><a xlink:href="../../grammar_diagrams#subquery"><rect class="rule" x="25" y="240" width="75" height="25"/><text class="text" x="35" y="257">subquery</text></a><rect class="literal" x="25" y="275" width="38" height="25" rx="7"/><text class="text" x="35" y="292">ON</text><rect class="literal" x="73" y="275" width="80" height="25" rx="7"/><text class="text" x="83" y="292">CONFLICT</text><a xlink:href="../../grammar_diagrams#conflict-target"><rect class="rule" x="183" y="275" width="106" height="25"/><text class="text" x="193" y="292">conflict_target</text></a><a xlink:href="../../grammar_diagrams#conflict-action"><rect class="rule" x="319" y="275" width="106" height="25"/><text class="text" x="329" y="292">conflict_action</text></a><a xlink:href="../../../grammar_diagrams#returning-clause"><rect class="rule" x="25" y="340" width="121" height="25"/><text class="text" x="35" y="357">returning_clause</text></a></svg>

#### returning_clause

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="565" height="122" viewbox="0 0 565 122"><path class="connector" d="M0 21h5m90 0h30m26 0h409m-450 0q5 0 5 5v48q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h180m24 0h181q5 0 5 5v19q0 5-5 5m-244 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m103 0h20m-224 0q5 0 5 5v23q0 5 5 5h199q5 0 5-5v-23q0-5 5-5m5 0h25q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="90" height="24" rx="7"/><text class="text" x="15" y="21">RETURNING</text><rect class="literal" x="125" y="5" width="26" height="24" rx="7"/><text class="text" x="135" y="21">*</text><rect class="literal" x="320" y="34" width="24" height="24" rx="7"/><text class="text" x="330" y="50">,</text><a xlink:href="../../grammar_diagrams#output-expression"><rect class="rule" x="145" y="63" width="136" height="24"/><text class="text" x="155" y="79">output_expression</text></a><rect class="literal" x="331" y="63" width="36" height="24" rx="7"/><text class="text" x="341" y="79">AS</text><a xlink:href="../../grammar_diagrams#output-name"><rect class="rule" x="397" y="63" width="103" height="24"/><text class="text" x="407" y="79">output_name</text></a></svg>

#### column_values

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="178" height="92" viewbox="0 0 178 92"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h57m24 0h57q5 0 5 5v19q0 5-5 5m-133 0h20m88 0h20m-123 0q5 0 5 5v19q0 5 5 5h5m74 0h19q5 0 5-5v-19q0-5 5-5m5 0h25"/><rect class="literal" x="77" y="5" width="24" height="24" rx="7"/><text class="text" x="87" y="21">,</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="45" y="34" width="88" height="24"/><text class="text" x="55" y="50">expression</text></a><rect class="literal" x="45" y="63" width="74" height="24" rx="7"/><text class="text" x="55" y="79">DEFAULT</text></svg>

#### conflict_target

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="505" height="125" viewbox="0 0 505 125"><path class="connector" d="M0 52h25m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h66m24 0h66q5 0 5 5v20q0 5-5 5m-151 0h20m106 0h20m-141 0q5 0 5 5v20q0 5 5 5h5m83 0h28q5 0 5-5v-20q0-5 5-5m5 0h30m25 0h30m65 0h10m74 0h20m-184 0q5 0 5 5v8q0 5 5 5h159q5 0 5-5v-8q0-5 5-5m5 0h20m-490 0q5 0 5 5v50q0 5 5 5h5m38 0h10m98 0h10m122 0h182q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="35" width="25" height="25" rx="7"/><text class="text" x="35" y="52">(</text><rect class="literal" x="141" y="5" width="24" height="25" rx="7"/><text class="text" x="151" y="22">,</text><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="100" y="35" width="106" height="25"/><text class="text" x="110" y="52">column_name</text></a><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="100" y="65" width="83" height="25"/><text class="text" x="110" y="82">expression</text></a><rect class="literal" x="256" y="35" width="25" height="25" rx="7"/><text class="text" x="266" y="52">)</text><rect class="literal" x="311" y="35" width="65" height="25" rx="7"/><text class="text" x="321" y="52">WHERE</text><a xlink:href="../../grammar_diagrams#condition"><rect class="rule" x="386" y="35" width="74" height="25"/><text class="text" x="396" y="52">condition</text></a><rect class="literal" x="25" y="95" width="38" height="25" rx="7"/><text class="text" x="35" y="112">ON</text><rect class="literal" x="73" y="95" width="98" height="25" rx="7"/><text class="text" x="83" y="112">CONSTRAINT</text><a xlink:href="../../../grammar_diagrams#constraint-name"><rect class="rule" x="181" y="95" width="122" height="25"/><text class="text" x="191" y="112">constraint_name</text></a></svg>

#### conflict_action

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="564" height="110" viewbox="0 0 564 110"><path class="connector" d="M0 22h25m38 0h10m77 0h409m-549 0q5 0 5 5v50q0 5 5 5h5m38 0h10m68 0h10m43 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h41m24 0h41q5 0 5 5v20q0 5-5 5m-5 0h50m65 0h10m74 0h20m-184 0q5 0 5 5v8q0 5 5 5h159q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="38" height="25" rx="7"/><text class="text" x="35" y="22">DO</text><rect class="literal" x="73" y="5" width="77" height="25" rx="7"/><text class="text" x="83" y="22">NOTHING</text><rect class="literal" x="25" y="65" width="38" height="25" rx="7"/><text class="text" x="35" y="82">DO</text><rect class="literal" x="73" y="65" width="68" height="25" rx="7"/><text class="text" x="83" y="82">UPDATE</text><rect class="literal" x="151" y="65" width="43" height="25" rx="7"/><text class="text" x="161" y="82">SET</text><rect class="literal" x="260" y="35" width="24" height="25" rx="7"/><text class="text" x="270" y="52">,</text><a xlink:href="../../grammar_diagrams#update-item"><rect class="rule" x="224" y="65" width="96" height="25"/><text class="text" x="234" y="82">update_item</text></a><rect class="literal" x="370" y="65" width="65" height="25" rx="7"/><text class="text" x="380" y="82">WHERE</text><a xlink:href="../../../grammar_diagrams#condition"><rect class="rule" x="445" y="65" width="74" height="25"/><text class="text" x="455" y="82">condition</text></a></svg>

### Grammar

```
insert ::= [ WITH [ RECURSIVE ] with_query [, ...] ]
       INSERT INTO table_name [ AS alias ] [ ( column_names ) ]
       { DEFAULT VALUES | VALUES ( column_values ) [, ... ] | subquery }
       [ ON CONFLICT [ conflict_target ] conflict_action ]
       [ RETURNING { * | output_expression [ [ AS ] output_name ] [,...] } ] 

column_values ::= { expression | DEFAULT } [, ... ]

conflict_target = ( { column_name | expression } [, ...] ) [ WHERE condition ] |
       ON CONSTRAINT constraint_name

conflict_action = { DO NOTHING |
       DO UPDATE SET update_item [, ...] [ WHERE condition ] }

update_item = { column_name = column_value |
       ( column_names ) = [ ROW ] ( column_values ) |
       ( column_names ) = ( query ) }
```

Where

- `qualified_name` and `name` are identifiers.
- `column_names` is a comma-separated list of columns names (identifiers).

## Semantics

 - An error is raised if the specified table does not exist. 
 - An error is raised if a specified column does not exist.
 - Each of the primary key columns must have a non-null value.
 - Constraints must be satisfied.  

### `VALUES` Clause
 - Each of the values list must have the same length as the columns list.
 - Each value must be convertible to its corresponding (by position) column type.
 - Each value literal can be an expression.

### `ON CONFLICT` Clause

- The target table must have at least one column (list) with either a unique index
or a unique constraint. We shall refer to this as a unique key. The argument of VALUES
is a relation that must include at least one of the target table's unique keys.
Some of the values of this unique key might be new, and others might already exist
in the target table.

- The basic aim of INSERT ON CONFLICT is simply to insert the rows with new values of
the unique key and to update the rows with existing values of the unique key to
set the values of the remaining specified columns to those in the VALUES relation.
In this way, the net effect is either to insert or to update; and for this reason
the INSERT ON CONFLICT variant is often colloquially referred to as "upsert".


## Examples

First, the bare insert. Create a sample table.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```


Insert some rows.

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```


Check the inserted rows.


```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
  3 |  4 |  5 | c
```


Next, a basic "upsert" example. Re-create and re-populate the sample table.

```sql
postgres=# DROP TABLE IF EXISTS sample CASCADE;
```

```sql
postgres=# CREATE TABLE sample(
  id int  CONSTRAINT sample_id_pk PRIMARY KEY,
  c1 text CONSTRAINT sample_c1_NN NOT NULL,
  c2 text CONSTRAINT sample_c2_NN NOT NULL);
```

```sql
postgres=# INSERT INTO sample(id, c1, c2)
  VALUES (1, 'cat'    , 'sparrow'),
         (2, 'dog'    , 'blackbird'),
         (3, 'monkey' , 'thrush');
```


Check the inserted rows.


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY id;
```

```
 id |   c1   |    c2     
----+--------+-----------
  1 | cat    | sparrow
  2 | dog    | blackbird
  3 | monkey | thrush
```


Demonstrate "on conflict do nothing". In this case, we don't need to specify the conflict target.


```sql
postgres=# INSERT INTO sample(id, c1, c2)
  VALUES (3, 'horse' , 'pigeon'),
         (4, 'cow'   , 'robin')
  ON CONFLICT
  DO NOTHING;
```


Check the result.
The non-conflicting row with id = 4 is inserted, but the conflicting row with id = 3 is NOT updated.


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY id;
```

```
 id |   c1   |    c2     
----+--------+-----------
  1 | cat    | sparrow
  2 | dog    | blackbird
  3 | monkey | thrush
  4 | cow    | robin
```

Demonstrate the real "upsert". In this case, we DO need to specify the conflict target. Notice the use of the
EXCLUDED keyword to specify the conflicting rows in the to-be-upserted relation.


```sql
postgres=# INSERT INTO sample(id, c1, c2)
  VALUES (3, 'horse' , 'pigeon'),
         (5, 'tiger' , 'starling')
  ON CONFLICT (id)
  DO UPDATE SET (c1, c2) = (EXCLUDED.c1, EXCLUDED.c2);

```

Check the result.
The non-conflicting row with id = 5 is inserted, and the conflicting row with id = 3 is updated.


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY id;
```

```
 id |  c1   |    c2     
----+-------+-----------
  1 | cat   | sparrow
  2 | dog   | blackbird
  3 | horse | pigeon
  4 | cow   | robin
  5 | tiger | starling
```

We can make the "update" happen only for a specified subset of the
excluded rows. We illustrate this by attempting to insert two conflicting rows
(with id = 4 and id = 5) and one non-conflicting row (with id = 6).
And we specify that the existing row with c1 = 'tiger' should not be updated
with "WHERE sample.c1 <> 'tiger'".

```sql
INSERT INTO sample(id, c1, c2)
  VALUES (4, 'deer'   , 'vulture'),
         (5, 'lion'   , 'hawk'),
         (6, 'cheeta' , 'chaffinch')
  ON CONFLICT (id)
  DO UPDATE SET (c1, c2) = (EXCLUDED.c1, EXCLUDED.c2)
  WHERE sample.c1 <> 'tiger';

```

Check the result.
The non-conflicting row with id = 6 is inserted;  the conflicting row with id = 4 is updated;
but the conflicting row with id = 5 (and c1 = 'tiger') is NOT updated;


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY id;
```

```
 id |   c1   |    c2     
----+--------+-----------
  1 | cat    | sparrow
  2 | dog    | blackbird
  3 | horse  | pigeon
  4 | deer   | vulture
  5 | tiger  | starling
  6 | cheeta | chaffinch
```

Notice that this restriction is legal too:

```
WHERE EXCLUDED.c1 <> 'lion'
```

Finally, a slightly more elaborate "upsert" example. Re-create and re-populate the sample table.
Notice that id is a self-populating surrogate primary key and that c1 is a business unique key.

```sql
postgres=# DROP TABLE IF EXISTS sample CASCADE;
```

```sql
CREATE TABLE sample(
  id INTEGER GENERATED ALWAYS AS IDENTITY CONSTRAINT sample_id_pk PRIMARY KEY,
  c1 TEXT CONSTRAINT sample_c1_NN NOT NULL CONSTRAINT sample_c1_unq unique,
  c2 TEXT CONSTRAINT sample_c2_NN NOT NULL);
```

```sql
INSERT INTO sample(c1, c2)
  VALUES ('cat'   , 'sparrow'),
         ('deer'  , 'thrush'),
         ('dog'   , 'blackbird'),
         ('horse' , 'vulture');
```


Check the inserted rows.


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY c1;
```

```
 id |  c1   |    c2     
----+-------+-----------
  1 | cat   | sparrow
  2 | deer  | thrush
  3 | dog   | blackbird
  4 | horse | vulture
```

Now do the upsert. Notice that we illustrate the usefulness
of the WITH clause to define the to-be-upserted relation
before the INSERT clause and use a subselect instead of
a VALUES clause. We also specify the conflict column(s)
indirectly by mentioniing the name of the unique constrained
that covers them.


```sql
postgres=# WITH to_be_upserted AS (
  SELECT c1, c2 FROM (VALUES
    ('cat'   , 'chaffinch'),
    ('deer'  , 'robin'),
    ('lion'  , 'duck'),
    ('tiger' , 'pigeon')
   )
  AS t(c1, c2)
  )
  INSERT INTO sample(c1, c2) SELECT c1, c2 FROM to_be_upserted
  ON CONFLICT ON CONSTRAINT sample_c1_unq
  DO UPDATE SET c2 = EXCLUDED.c2;
```

Check the inserted rows.


```sql
postgres=# SELECT id, c1, c2 FROM sample ORDER BY c1;
```

```
 id |  c1   |    c2     
----+-------+-----------
  1 | cat   | chaffinch
  2 | deer  | robin
  3 | dog   | blackbird
  4 | horse | vulture
  7 | lion  | duck
  8 | tiger | pigeon
```


## See Also

[`CREATE TABLE`](../ddl_create_table)
[`SELECT`](../dml_select)
[Other YSQL Statements](..)
