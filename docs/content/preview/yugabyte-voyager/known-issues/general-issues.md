---
title: General guidelines
linkTitle: General
headcontent: General guide when migrating data from MySQL, Oracle, or PostgreSQL.
description: General guide and suggested workarounds for migrating data from MySQL, Oracle, or PostgreSQL.
menu:
  preview_yugabyte-voyager:
    identifier: general-issues
    parent: known-issues
    weight: 100
type: docs
rightNav:
  hideH3: true
---

Review and explore the suggested workarounds for multiple areas when migrating data from MySQL, Oracle, or PostgreSQL to YugabyteDB.

## Contents

- [Index on timestamp column should be imported as ASC (Range) index to avoid sequential scans](#index-on-timestamp-column-should-be-imported-as-asc-range-index-to-avoid-sequential-scans)

- [Exporting data with names for tables/functions/procedures using special characters/whitespaces fails](#exporting-data-with-names-for-tables-functions-procedures-using-special-characters-whitespaces-fails)

- [Importing with case-sensitive schema names](#importing-with-case-sensitive-schema-names)

### Index on timestamp column should be imported as ASC (Range) index to avoid sequential scans

**GitHub**: [Issue #49](https://github.com/yugabyte/yb-voyager/issues/49)

**Description**: If there is an index on a timestamp column, the index should be imported as a range index automatically, as most queries relying on timestamp columns use range predicates. This avoids sequential scans and makes indexed scans accessible.

**Workaround**: Manually add the ASC (range) clause to the exported files.

**Example**

An example schema on the source database is as follows:

```sql
CREATE INDEX ON timestamp_demo (ts);
```

Suggested change to the schema is to add the `ASC` clause as follows:

```sql
CREATE INDEX ON timestamp_demo (ts ASC);
```

---

### Exporting data with names for tables/functions/procedures using special characters/whitespaces fails

**GitHub**: [Issue #636](https://github.com/yugabyte/yb-voyager/issues/636), [Issue #688](https://github.com/yugabyte/yb-voyager/issues/688), [Issue #702](https://github.com/yugabyte/yb-voyager/issues/702)

**Description**: If you define complex names for your source database tables/functions/procedures using backticks or double quotes for example, \`abc xyz\` , \`abc@xyz\`, or "abc@123", the migration hangs during the export data step.

**Workaround**: Rename the objects (tables/functions/procedures) on the source database to a name without special characters.

**Example**

An example schema on the source MySQL database is as follows:

```sql
CREATE TABLE `xyz abc`(id int);
INSERT INTO `xyz abc` VALUES(1);
INSERT INTO `xyz abc` VALUES(2);
INSERT INTO `xyz abc` VALUES(3);
```

The exported schema is as follows:

```sql
CREATE TABLE "xyz abc" (id bigint);
```

The preceding example may hang or result in an error.

---

### Importing with case-sensitive schema names

**GitHub**: [Issue #422](https://github.com/yugabyte/yb-voyager/issues/422)

**Description**: If you migrate your database using a case-sensitive schema name, the migration will fail with a "no schema has been selected" or "schema already exists" error(s).

**Workaround**: Currently, yb-voyager does not support case-sensitive schema names; all schema names are assumed to be case-insensitive (lower-case). If required, you may alter the schema names to a case-sensitive alternative post-migration using the ALTER SCHEMA command.

**Example**

An example yb-voyager import-schema command with a case-sensitive schema name is as follows:

```sh
yb-voyager import schema --target-db-name voyager
    --target-db-hostlocalhost
    --export-dir .
    --target-db-password password
    --target-db-user yugabyte
    --target-db-schema "\"Test\""
```

The preceding example will result in an error as follows:

```output
ERROR: no schema has been selected to create in (SQLSTATE 3F000)
```

Suggested changes to the schema can be done using the following steps:

1. Change the case sensitive schema name during schema migration as follows:

    ```sh
    yb-voyager import schema --target-db-name voyager
    --target-db-hostlocalhost
    --export-dir .
    --target-db-password password
    --target-db-user yugabyte
    --target-db-schema test
    ```

1. Alter the schema name post migration as follows:

    ```sh
    ALTER SCHEMA "test" RENAME TO "Test";
    ```
