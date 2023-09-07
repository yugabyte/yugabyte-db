---
title: Data type mapping from Oracle to YugabyteDB
linkTitle: Data type mapping
description: Refer to the data type mapping table when migrating data from Oracle to YugabyteDB using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: datatype-mapping-oracle
    parent: reference-voyager
    weight: 102
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../datatype-mapping-mysql/" class="nav-link">
      MySQL to YugabytyeDB
    </a>
  </li>
  <li class="active">
    <a href="../datatype-mapping-oracle/" class="nav-link">
      Oracle to YugabyteDB
    </a>
  </li>
</ul>

The following table includes a list of supported data type mappings for migrating data from Oracle to YugabyteDB using YugabyteDB Voyager:

| Oracle data type | Maps to YugabyeDB as | Comments |
| :--------------- | :------------------- | :------- |
| CHAR | CHAR |
| NCHAR | CHAR | Unsupported in [Live migration](../../migrate/live-migrate/), or [Live migration with fall-forward](../../migrate/live-fall-forward/) |
| VARCHAR2 | VARCHAR |
| NVARCHAR2 | VARCHAR | Unsupported in [Live migration](../../migrate/live-migrate/), or [Live migration with fall-forward](../../migrate/live-fall-forward/) |
| RAW | BYTEA |
| LONG RAW | BYTEA |
| DATE | DATE |
| TIMESTAMP WITH TIMEZONE | TIMESTAMP WITH TIMEZONE |
| TIMESTAMP WITH LOCAL TIME ZONE | TIMESTAMP WITH TIMEZONE |
| TIMESTAMP | TIMESTAMP |
| INTERVAL YEAR TO MONTH | INTERVAL YEAR TO MONTH |
| INTERVAL DAY TO SECOND | INTERVAL DAY TO SECOND |
| LONG | TEXT |
| FLOAT | DOUBLE PRECISION |
| BINARY_FLOAT | DOUBLE PRECISION |
| BINARY_DOUBLE | DOUBLE PRECISION |
| NUMBER | NUMERIC |
| NUMBER(3) | SMALLINT |
| NUMBER(3,2) | NUMERIC(3,2) |
| NUMBER(2,7) | NUMERIC |
| NUMBER(6,-2) | NUMERIC(6,-2) | Results in an error because negative scale is currently not supported. Refer to the [GitHub issue](https://github.com/yugabyte/yb-voyager/issues/779) for more details.
| BLOB | BYTEA | Data is ignored during export. Only the schema migration is allowed. Please use another mechanism to load the values. |
| CLOB | TEXT | Data migration is allowed to a limit of 235MB per file. Unsupported in [Live migration](../../migrate/live-migrate/), [Live migration with fall-forward](../../migrate/live-fall-forward/) |
| NCLOB | TEXT | Data migration is allowed to a limit of 235MB per file. Unsupported in [Live migration](../../migrate/live-migrate/), [Live migration with fall-forward](../../migrate/live-fall-forward/) |
| BFILE | BYTEA | Not supported. |
| ROWID | OID | Currently, import schema is supported. Data import results in an error. |
| UROWID [(size)] | OID | Currently, import schema is supported. Data import results in an error. |
| SYS.AnyData | ANYDATA | Not supported. |
| SYS.AnyType | ANYTYPE | Not supported. |
| SYS.AnyDataSet | ANYDATASET | Not supported. |
| XMLType | XML | Currently, import schema is supported. Data import results in an error. |
| URIType | URITYPE | Not supported. |
| Objects | | Not supported. |
| REF | | Not supported. |
| Nested tables | Composite type |  Unsupported when used with [BETA_FAST_DATA_EXPORT](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), and in [Live migration](../../migrate/live-migrate/) or [Live migration with fall-forward](../../migrate/live-fall-forward/)|
| VARRAY | Composite type |  Unsupported when used with [BETA_FAST_DATA_EXPORT](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle). |

### ANSI supported data types

The following table list the ANSI supported data types that can be mapped to YugabyteDB:

| ANSI supported data type | Maps to YugabyeDB as |
| :----------------------- | :------------------- |
| CHARACTER(n) | CHAR(n) |
| CHAR(n) | CHAR(n) |
| CHARACTER VARYING(n) | VARCHAR(n) |
| CHAR VARYING(n) | VARCHAR(n) |
| NATIONAL CHARACTER(n) | CHAR(n) |
| NATIONAL CHAR(n) | CHAR(n) |
| NCHAR(n) | CHAR(n) |
| NATIONAL CHARACTER VARYING(n) | VARCHAR(n) |
| NATIONAL CHAR VARYING(n) | VARCHAR(n) |
| NCHAR VARYING(n) | VARCHAR(n) |
| NUMERIC[(p,s)] | REAL |
| DECIMAL[(p,s)] | REAL |
| INTEGER | NUMERIC(38) |
| INT | NUMERIC(38) |
| SMALLINT | NUMERIC(38) |
| FLOAT | DOUBLE PRECISION |
| DOUBLE PRECISION | DOUBLE PRECISION |
| REAL | DOUBLE PRECISION |

## Learn more

- [Data modeling](../data-modeling)