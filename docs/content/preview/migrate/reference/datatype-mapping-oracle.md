---
title: Data type mapping from Oracle to YugabyteDB
linkTitle: Data type mapping
description: Refer to the data type mapping table when migrating data from Oracle to YugabyteDB using YugabyteDB Voyager.
menu:
  preview:
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
| NCHAR | CHAR |
| VARCHAR2 | VARCHAR |
| NVARCHAR2 | VARCHAR |
| RAW | BYTEA |
| LONG RAW | BYTEA |
| DATE | TIMESTAMP |
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
| NUMBER(3,2) | REAL |
| NUMBER(2,7) | NUMERIC |
| NUMBER(6,-2) | REAL |
| BLOB | BYTEA | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| CLOB | TEXT | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| NCLOB | TEXT | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| BFILE | BYTEA | Analysis of BFILES is still in progress and is currently unsupported. |
| ROWID | OID | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| UROWID [(size)] | OID | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| SYS.AnyData | ANYDATA | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| SYS.AnyType | ANYTYPE | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| SYS.AnyDataSet | ANYDATASET | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| XMLType | XML | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |
| URIType | URITYPE | Currently, import schema is not supported. Failed SQL statements can be found in `export-dir/schema/failed.sql` |

#### ANSI supported data types

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