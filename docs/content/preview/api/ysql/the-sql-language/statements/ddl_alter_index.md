---
title: ALTER INDEX statement [YSQL]
headerTitle: ALTER INDEX
linkTitle: ALTER INDEX
description: Use the `ALTER INDEX` statement to change the definition of an index.
menu:
  preview:
    identifier: ddl_alter_index
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_alter_index/
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


#### SET TABLESPACE *tablespace_name*

Asynchronously change the tablespace of an existing index. 
The tablespace change will immediately reflect in the config of the index, however the tablet move by the load balancer happens in the background. 
While the load balancer is performing the move it is perfectly safe from a correctness perspective to do reads and writes, however some query optimization that happens based on the data location may be off while data is being moved.

{{< note title="Note" >}}

This is a BETA feature, and we do not recommend running this command on a production database. You will see a warning when running the command, but you can ignore it. Here is an example below:

```sql
yugabyte=# ALTER INDEX bank_transactions_idx SET TABLESPACE eu_central_1_tablespace;
WARNING:  'tablespace_alteration' is a beta feature!
LINE 1: ALTER INDEX bank_transactions_idx SET TABLESPACE eu_central_1...
                                         ^
HINT:  To suppress this warning, set the 'ysql_beta_feature_tablespace_alteration' yb-tserver gflag to true.
(Set 'ysql_beta_features' yb-tserver gflag to true to suppress the warning for all beta features.)
```

{{< /note >}}

## See also

- [`CREATE INDEX`](../ddl_create_index)
- [`DROP INDEX`](../ddl_drop_index)
