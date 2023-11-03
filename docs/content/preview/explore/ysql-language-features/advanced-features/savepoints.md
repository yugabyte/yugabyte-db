---
title: Savepoints
linkTitle: Savepoints
description: Savepoints in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: advanced-features-savepoints
    parent: advanced-features
    weight: 235
type: docs
---

This document provides an overview of YSQL savepoints, and demonstrates how to use them to checkpoint your progress within a transaction. The `SAVEPOINT` command establishes a new savepoint in the current transaction.

## Overview

A savepoint is a special mark inside a transaction that allows you to roll back all commands that are executed after it was established, restoring the transaction state to what it was at the time of the savepoint, all without abandoning the transaction.

Savepoints are implemented in PostgreSQL using subtransactions.

The relevant savepoint commands are:

* [`SAVEPOINT <name>`](../../../../api/ysql/the-sql-language/statements/savepoint_create/) creates a savepoint.

* [`RELEASE SAVEPOINT <name>`](../../../../api/ysql/the-sql-language/statements/savepoint_release/) forgets about a savepoint. Note that you can use the same savepoint name more than once, so if there was an earlier savepoint with the same name, the name will now refer to that earlier savepoint. (In other words, the new savepoint gets popped off the stack of savepoints for that name.)

* [`ROLLBACK TO SAVEPOINT <name>`](../../../../api/ysql/the-sql-language/statements/savepoint_rollback) rolls back to the database state as of the given savepoint, discarding all changes created after that savepoint, including the creation of new savepoints. Preserves the referenced savepoint, so that after one rollback it can be rolled back to again.

## Example

{{% explore-setup-single %}}

1. Create a sample table.

    ```plpgsql
    CREATE TABLE sample(k int PRIMARY KEY, v int);
    ```

1. Begin a transaction and insert some rows.

    ```plpgsql
    BEGIN TRANSACTION;
    INSERT INTO sample(k, v) VALUES (1, 2);
    SAVEPOINT test;
    INSERT INTO sample(k, v) VALUES (3, 4);
    ```

1. Now, check the rows in this table.

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

1. Rollback to savepoint `test` and check the rows again. Note that the second row no longer appears.

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

1. You can even add a new row at this point. If you then commit the transaction, only the first and third rows inserted persist:

    ```plpgsql
    INSERT INTO sample(k, v) VALUES (5, 6);
    COMMIT;
    SELECT * FROM SAMPLE;
    ```

    ```output
     k  | v
    ----+----
      5 |  6
      1 |  2
    (2 rows)
    ```

## Further reading

Refer to [`RELEASE SAVEPOINT <name>`](../../../../api/ysql/the-sql-language/statements/savepoint_release/) and [`ROLLBACK TO SAVEPOINT <name>`](../../../../api/ysql/the-sql-language/statements/savepoint_rollback/) for more examples.
