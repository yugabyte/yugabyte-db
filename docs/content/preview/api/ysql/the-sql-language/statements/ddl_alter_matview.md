---
title: ALTER MATERIALIZED VIEW statement [YSQL]
headerTitle: ALTER MATERIALIZED VIEW
linkTitle: ALTER MATERIALIZED VIEW
description: Use the `ALTER MATERIALIZED VIEW` statement to change the definition of a materialized view.
menu:
  preview:
    identifier: ddl_alter_matview
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_alter_matview/
type: docs
---

## Synopsis

Use the `ALTER MATERIALIZED VIEW` statement to change the definition of a materialized view.

## Syntax

{{%ebnf%}}
  alter_materialized_view,
  alter_materialized_view_action,
{{%/ebnf%}}

## Semantics

### *alter_materialized_view_action*

Specify one of the following actions.

#### RENAME TO *new_name*

Rename the materialized view to the specified name.

{{< note title="Note" >}}

Renaming a materialized view is a non-blocking metadata change operation.

{{< /note >}}


#### SET TABLESPACE *tablespace_name*

Asynchronously change the tablespace of an existing materialized view. 
The tablespace change will immediately reflect in the config of the materialized view, however the tablet move by the load balancer happens in the background. 
While the load balancer is performing the move it is perfectly safe from a correctness perspective to do reads and writes, however some query optimization that happens based on the data location may be off while data is being moved.


##### Example

```sql
yugabyte=# ALTER MATERIALIZED VIEW mv SET TABLESPACE mv_tblspace2;
```

```output
yugabyte=# NOTICE:  Data movement for table mv is successfully initiated.
yugabyte=# DETAIL:  Data movement is a long running asynchronous process and can be monitored by checking the tablet placement in http://<YB-Master-host>:7000/tables
```

## See also

- [`CREATE MATERIALZIED VIEW`](../ddl_create_matview)
- [`DROP MATERIALIZED VIEW`](../ddl_drop_matview)
