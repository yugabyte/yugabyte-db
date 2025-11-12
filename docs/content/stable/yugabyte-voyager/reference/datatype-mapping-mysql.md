---
title: Data type mapping from MySQL to YugabyteDB
linkTitle: Data type mapping
description: Refer to the data type mapping table when migrating data from MySQL to YugabyteDB using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: datatype-mapping-mysql
    parent: reference-voyager
    weight: 102
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../datatype-mapping-mysql/" class="nav-link">
      MySQL to YugabytyeDB
    </a>
  </li>
  <li>
    <a href="../datatype-mapping-oracle/" class="nav-link">
      Oracle to YugabyteDB
    </a>
  </li>
</ul>

The following table includes a list of supported data type mappings for migrating data from MySQL to YugabyteDB using YugabyteDB Voyager:

| MySQL data type | Maps to YugabyteDB as | Description |
| :-------------- | :------------------- | :---------- |
| BINARY | BYTEA |
| VARBINARY | BYTEA |
| BIT | BIT |
| BOOLEAN | SMALLINT | As BOOLEAN type is a synonym of TINYINT(1) in [MySQL](https://dev.mysql.com/doc/refman/8.0/en/numeric-type-syntax.html), it is converted to SMALLINT in YugabyteDB, but if your use case needs it to be converted to BOOLEAN in YugabyteDB, modify the schema manually for all the respective BOOLEAN columns. |
| CHAR | CHAR |
| VARCHAR | VARCHAR |
| TINYTEXT | TEXT |
| MEDIUMTEXT | TEXT |
| TEXT | TEXT |
| LONGTEXT | TEXT |
| TINYBLOB | BYTEA | Data is ignored during export. |
| BLOB | BYTEA | Data is ignored during export. |
| MEDIUMBLOB | BYTEA | Data is ignored during export. |
| LONGBLOB | BYTEA | Data is ignored during export. |
| DATE | DATE |
| TIME | TIME WITHOUT TIMEZONE |
| DATETIME | TIMESTAMP WITHOUT TIMEZONE |
| TIMESTAMP | TIMESTAMP |
| YEAR | SMALLINT |
| DEC | DECIMAL |
| DECIMAL | DECIMAL |
|NUMERIC | DECIMAL |
| FIXED | DECIMAL |
| ENUM | ENUM | A user-defined ENUM type is created |
| FLOAT | DOUBLE PRECISION |
| REAL | DOUBLE PRECISION |
| DOUBLE | DOUBLE PRECISION |
| JSON/JSONB |JSON |
| UUID | BYTEA | Not a separate datatype; it is created using functions. |
| TINYINT SIGNED | SMALLINT |
| TINYINT UNSIGNED | SMALLINT |
| SMALLINT SIGNED | INTEGER |
| SMALLINT UNSIGNED | INTEGER |
| MEDUIMINT SIGNED | INTEGER |
| MEDUIMINT UNSIGNED | INTEGER |
| INT/INTEGER SIGNED | BIGINT |
| INT/INTEGER UNSIGNED | BIGINT |
| BIGINT SIGNED | BIGINT |
| BIGINT UNSIGNED | NUMERIC(20) |

## Learn more

- [Data modeling](../../../develop/data-modeling)
