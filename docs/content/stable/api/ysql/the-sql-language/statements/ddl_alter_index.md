---
title: ALTER INDEX statement [YSQL]
headerTitle: ALTER INDEX
linkTitle: ALTER INDEX
description: Use the `ALTER INDEX` statement to change the definition of an index.
menu:
  stable:
    identifier: ddl_alter_index
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER INDEX` statement to change the definition of an index.

## Syntax

{{%ebnf%}}
  alter_index,
  alter_index_action,
{{%/ebnf%}}

## Semantics

### *alter_index_action*

Specify one of the following actions.

#### RENAME TO *new_name*

Rename the index to the specified index name.

{{< note title="Note" >}}

Renaming an index is a non-blocking metadata change operation.

{{< /note >}}

#### ALTER [ COLUMN ] column_number SET STATISTICS *integer*

Set the per-column statistics-gathering target for subsequent ANALYZE operations. It can only be used on index columns that are defined as an expression. 
Since expressions lack a unique name, we refer to them using the ordinal number of the index column. 
The value can be set in the range 0 to 10000. The default `-1` uses the system default statistics target (`default_statistics_target`). 

```sql
yugabyte=# CREATE TABLE attmp (initial int4, a int4 default 3, b name,d float8,e float4);
yugabyte=# CREATE INDEX attmp_idx ON attmp (a, (d + e), b);
yugabyte=# ALTER INDEX attmp_idx ALTER COLUMN 0 SET STATISTICS 1000;
ERROR:  column number must be in range from 1 to 32767
LINE 1: ALTER INDEX attmp_idx ALTER COLUMN 0 SET STATISTICS 1000;
yugabyte=# ALTER INDEX attmp_idx ALTER COLUMN 1 SET STATISTICS 1000;
ERROR:  cannot alter statistics on non-expression column "a" of index "attmp_idx"
HINT:  Alter statistics on table column instead.

yugabyte=# ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS 1000;
yugabyte=# \d+ attmp_idx
                        Index "public.attmp_idx"
 Column |       Type       | Key? | Definition | Storage | Stats target
--------+------------------+------+------------+---------+--------------
 a      | integer          | yes  | a          | plain   |
 expr   | double precision | yes  | (d + e)    | plain   | 1000
 b      | name             | yes  | b          | plain   |
lsm, for table "public.attmp"
```

#### SET TABLESPACE *tablespace_name*

Asynchronously change the tablespace of an existing index. 
The tablespace change will immediately reflect in the config of the index, however the tablet move by the load balancer happens in the background. 
While the load balancer is performing the move it is perfectly safe from a correctness perspective to do reads and writes, however some query optimization that happens based on the data location may be off while data is being moved.


##### Example

```sql
yugabyte=# ALTER INDEX bank_transactions_idx SET TABLESPACE eu_central_1_tablespace;
```

```output
NOTICE:  Data movement for index bank_transactions_idx is successfully initiated.
DETAIL:  Data movement is a long running asynchronous process and can be monitored by checking the tablet placement in http://<YB-Master-host>:7000/tables
ALTER INDEX
```

## See also

- [`CREATE INDEX`](../ddl_create_index)
- [`DROP INDEX`](../ddl_drop_index)
