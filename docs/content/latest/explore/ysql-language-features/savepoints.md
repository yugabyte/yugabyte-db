---
title: Savepoints
linkTitle: Savepoints
description: Savepoints in YSQL
headcontent: Savepoints in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-savepoints
    parent: explore-ysql-language-features
    weight: 400
isTocNested: true
showAsideToc: true
---

This document provides an overview of YSQL Savepoints and demonstrates how they can be used to checkpoint your progress within a transaction.

## Overview

The `SAVEPOINT` command establishes a new savepoint within the current transaction. A savepoint is a special mark inside a transaction that allows all commands that are executed after it was established to be rolled back, restoring the transaction state to what it was at the time of the savepoint without abandoning the transaction.

Savepoints are implemented in PostgreSQL using subtransactions.

### Commands
The relevant savepoint commands are:

[`SAVEPOINT <name>`](../../../api/ysql/the-sql-language/statements/savepoint_create) -- creates a savepoint.

[`RELEASE SAVEPOINT <name>`](../../../api/ysql/the-sql-language/statements/savepoint_release) -- forgets about a savepoint. Note that a savepoint name could be reused, so if there was an earlier savepoint with the same name, the name will now refer to that earlier savepoint (the new savepoint gets popped off the stack of savepoints for that name).

[`ROLLBACK TO SAVEPOINT <name>`](../../../api/ysql/the-sql-language/statements/savepoint_rollback) -- rolls back to the database state as of the given savepoint, discarding all changes created after that savepoint, including the creation of new savepoints. Preserves the referenced savepoint, so that after one rollback it can be rolled back to again.

## Example

Create a sample table.

```plpgsql
CREATE TABLE sample(k int PRIMARY KEY, v int);
```

Begin a transaction and insert some rows.

```plpgsql
BEGIN TRANSACTION;
INSERT INTO sample(k, v) VALUES (1, 2);
SAVEPOINT test;
INSERT INTO sample(k, v) VALUES (3, 4);
```

Now, check the rows in this table:

```plpgsql
SELECT * FROM sample;
```

```output
 k  | v  
----+----
  1 |  2
  3 |  4
(2 rows)
```

And then, rollback to savepoint `test` and check the rows again. Note that the second row does not appear:

```plpgsql
ROLLBACK TO test;
SELECT * FROM sample;
```

```output
 k  | v  
----+----
  1 |  2
(1 row)
```

We can even add a new row at this point. If we then commit the transaction, only the first and third row inserted will persist:

```plpgsql
INSERT INTO sample(k, v) VALUES (5, 6);
COMMIT;
SELECT * FROM SAMPLE;
```

```output
 k  | v  
----+----
  1 |  2
  5 |  6
(2 rows)
```

Refer to the page for the [`RELEASE SAVEPOINT <name>`](../../../api/ysql/the-sql-language/statements/savepoint_release) and [`ROLLBACK TO SAVEPOINT <name>`](../../../api/ysql/the-sql-language/statements/savepoint_rollback) for more examples.
