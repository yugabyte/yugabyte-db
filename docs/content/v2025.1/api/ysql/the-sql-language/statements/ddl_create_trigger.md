---
title: CREATE TRIGGER statement [YSQL]
headerTitle: CREATE TRIGGER
linkTitle: CREATE TRIGGER
description: Use the CREATE TRIGGER statement to create a trigger.
menu:
  v2025.1_api:
    identifier: ddl_create_trigger
    parent: statements
type: docs
---

## Synopsis

Use the CREATE TRIGGER statement to create a trigger.

## Syntax

{{%ebnf%}}
  create_trigger,
  event
{{%/ebnf%}}

## Semantics

- the WHEN condition can be used to specify whether the trigger should be fired. For low-level triggers it can reference the old and/or new values of the row's columns.
- multiple triggers can be defined for the same event. In that case, they will be fired in alphabetical order by name.

### OR replace

The OR REPLACE option allows you to replace an existing trigger with a new one without first having to drop the old trigger. By automatically replacing the existing trigger, this option reduces the risk of errors that might arise from accidentally forgetting to drop a trigger before creating a new one.

## Examples

- Set up a table with triggers for tracking modification time and user (role).
    Use the pre-installed extensions `insert_username` and `moddatetime`.

    ```plpgsql
    CREATE EXTENSION insert_username;
    CREATE EXTENSION moddatetime;

    CREATE TABLE posts (
      id int primary key,
      content text,
      username text not null,
      moddate timestamp DEFAULT CURRENT_TIMESTAMP NOT NULL
    );

    CREATE TRIGGER insert_usernames
       BEFORE INSERT OR UPDATE ON posts
       FOR EACH ROW
       EXECUTE PROCEDURE insert_username (username);

    CREATE TRIGGER update_moddatetime
       BEFORE UPDATE ON posts
       FOR EACH ROW
       EXECUTE PROCEDURE moddatetime (moddate);
    ```

- Insert some rows.
    For each insert, the triggers should set the current role as `username` and the current timestamp as `moddate`.

    ```plpgsql
    SET ROLE yugabyte;
    INSERT INTO posts VALUES(1, 'desc1');

    SET ROLE postgres;
    INSERT INTO posts VALUES(2, 'desc2');
    INSERT INTO posts VALUES(3, 'desc3');

    SET ROLE yugabyte;
    INSERT INTO posts VALUES(4, 'desc4');

    SELECT * FROM posts ORDER BY id;
    ```

    ```output
     id | content | username |          moddate
    ----+---------+----------+----------------------------
      1 | desc1   | yugabyte | 2019-09-13 16:55:53.969907
      2 | desc2   | postgres | 2019-09-13 16:55:53.983306
      3 | desc3   | postgres | 2019-09-13 16:55:53.98658
      4 | desc4   | yugabyte | 2019-09-13 16:55:53.991315
    ```

  {{< note title="Note" >}}

  YSQL should have users `yugabyte` and (for compatibility) `postgres` created by default.

  {{< /note >}}

- Update some rows.
    For each update the triggers should set both `username`  and `moddate` accordingly.

    ```plpgsql
    UPDATE posts SET content = 'desc1_updated' WHERE id = 1;
    UPDATE posts SET content = 'desc3_updated' WHERE id = 3;

    SELECT * FROM posts ORDER BY id;
    ```

    ```caddyfile
     id |    content    | username |          moddate
    ----+---------------+----------+----------------------------
      1 | desc1_updated | yugabyte | 2019-09-13 16:56:27.623513
      2 | desc2         | postgres | 2019-09-13 16:55:53.983306
      3 | desc3_updated | yugabyte | 2019-09-13 16:56:27.634099
      4 | desc4         | yugabyte | 2019-09-13 16:55:53.991315
    ```

## Partitioned tables

Creating a row-level trigger on a partitioned table automatically results in an identical trigger being created on all of its existing partitions. Additionally, any partitions that are created or attached to the table later will also receive the same trigger. If a partition already has a trigger with the same name, an error will occur unless you use the CREATE OR REPLACE TRIGGER command, which replaces the existing trigger with the new one. When a partition is detached from its parent table, the triggers associated with it are removed automatically.

### BEFORE

For a BEFORE trigger, the WHEN condition is evaluated immediately before the trigger function is executed, or would be executed if the condition evaluates to TRUE. This makes using the WHEN clause functionally similar to performing the same check at the start of the trigger function. The NEW row seen by the condition reflects any modifications made by earlier triggers in the same operation.

BEFORE ROW triggers on INSERT cannot change which partition is the final destination for a new row.

### AFTER

In the case of an AFTER trigger, the WHEN condition is evaluated right after the row update is completed. The result of this evaluation determines whether an event is queued to fire the trigger at the end of the statement. If the WHEN condition evaluates to FALSE, no event is queued, and the trigger does not execute. This behavior can lead to significant performance improvements in statements that modify many rows, as the trigger is only fired for the rows that meet the condition.

## See also

- [DROP TRIGGER](../ddl_drop_trigger/)
- [INSERT](../dml_insert/)
- [UPDATE](../dml_update/)
- [DELETE](../dml_delete/)
