---
title: spi extension
headerTitle: spi extension
linkTitle: spi
description: Using the spi extension in YugabyteDB
menu:
  stable:
    identifier: extension-spi
    parent: pg-extensions
    weight: 20
type: docs
---

The [spi](https://www.postgresql.org/docs/11/contrib-spi.html) module provides several workable examples of using the Server Programming Interface (SPI) and triggers.

YugabyteDB supports the following four (of five &mdash; `timetravel` is not currently supported) extensions provided in the spi module:

* `autoinc` functions auto-increment fields.
* `insert_username` functions track who changed a table.
* `moddatetime` functions track last modification times.
* `refint` functions implement referential integrity.

## Example

1. Connect using `ysqlsh` and run the following commands:

    ```sql
    CREATE EXTENSION insert_username;
    CREATE EXTENSION moddatetime;
    ```

1. Set up a table with triggers for tracking modification time and user (role):

    ```sql
    CREATE TABLE spi_test (
      id int primary key,
      content text,
      username text not null,
      moddate timestamp DEFAULT CURRENT_TIMESTAMP NOT NULL
    );

    CREATE TRIGGER insert_usernames
      BEFORE INSERT OR UPDATE ON spi_test
      FOR EACH ROW
      EXECUTE PROCEDURE insert_username (username);

    CREATE TRIGGER update_moddatetime
      BEFORE UPDATE ON spi_test
      FOR EACH ROW
      EXECUTE PROCEDURE moddatetime (moddate);
    ```

1. Insert some rows. Each insert should add the current role as `username` and the current timestamp as `moddate`.

    ```sql
    SET ROLE yugabyte;
    INSERT INTO spi_test VALUES(1, 'desc1');

    SET ROLE postgres;
    INSERT INTO spi_test VALUES(2, 'desc2');
    INSERT INTO spi_test VALUES(3, 'desc3');

    SET ROLE yugabyte;
    INSERT INTO spi_test VALUES(4, 'desc4');

    SELECT * FROM spi_test ORDER BY id;
    ```

    ```output
     id | content | username |          moddate
    ----+---------+----------+----------------------------
      1 | desc1   | yugabyte | 2019-09-13 16:55:53.969907
      2 | desc2   | postgres | 2019-09-13 16:55:53.983306
      3 | desc3   | postgres | 2019-09-13 16:55:53.98658
      4 | desc4   | yugabyte | 2019-09-13 16:55:53.991315
    (4 rows)
    ```

    The `yugabyte` and (for compatibility) `postgres` YSQL users are created by default.

1. Update some rows. This should update both `username`  and `moddate` accordingly.

    ```sql
    UPDATE spi_test SET content = 'desc1_updated' WHERE id = 1;
    UPDATE spi_test SET content = 'desc3_updated' WHERE id = 3;

    SELECT * FROM spi_test ORDER BY id;
    ```

    ```output
    id |    content    | username |          moddate
    ----+---------------+----------+----------------------------
      1 | desc1_updated | yugabyte | 2019-09-13 16:56:27.623513
      2 | desc2         | postgres | 2019-09-13 16:55:53.983306
      3 | desc3_updated | yugabyte | 2019-09-13 16:56:27.634099
      4 | desc4         | yugabyte | 2019-09-13 16:55:53.991315
    (4 rows)
    ```
