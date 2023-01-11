---
title: Datatype mapping from Oracle to YugabyteDB
linkTitle: Datatype mapping
description: Refer to the datatype mapping table when migrating data from Oracle to YugabyuteDB using YugabyteDB Voyager.
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
      Oracle to yugabyteDB
    </a>
  </li>
</ul>

| Oracle datatype | Mapped to YugabyeDB | Comments |
| :-------------- | :------------------ | :------- |
| CHAR | char |
| NCHAR | char |
| VARCHAR2 | varchar |
| NVARCHAR2 | varchar |
| RAW | bytea |
| LONG RAW | bytea |
| DATE | timestamp |
| TIMESTAMP WITH TIMEZONE | timestamp with time zone |
| TIMESTAMP WITH LOCAL TIME ZONE | timestamp with time zone |
| TIMESTAMP | timestamp |
| INTERVAL YEAR TO MONTH | INTERVAL YEAR TO MONTH |
| INTERVAL DAY TO SECOND | INTERVAL DAY TO SECOND |
| LONG | text |
| FLOAT | double precision |
| BINARY_FLOAT | double precision |
| BINARY_DOUBLE | double precision |
| NUMBER | numeric |
| NUMBER(3) | smallint |
| NUMBER(3,2) | real |
| NUMBER(2,7) | numeric |
| NUMBER(6,-2) | real |
| BLOB | bytea | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| CLOB | text | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| NCLOB | text | Currently, import data is not supported for BLOB/CLOB. Only the schema migration is allowed. |
| BFILE | bytea | Analysis of BFILES is still in progress and is currently unsupported. |
| ROWID | oid | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| UROWID [(size)] | oid | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| SYS.AnyData | ANYDATA | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| SYS.AnyType | ANYTYPE | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| SYS.AnyDataSet | ANYDATASET | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| XMLType | xml | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |
| URIType | URITYPE | Currently, import schema is not supported. Failed SQL statements can be found in "export-dir/schema/failed.sql" |

### ANSI SUPPORTED DATA TYPES

| Oracle datatype | Mapped to YugabyeDB |
| :-------------- | :------------------ |
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
| INTEGER | numeric(38) |
| INT | numeric(38) |
| SMALLINT | numeric(38) |
| FLOAT | double precision |
| DOUBLE PRECISION | double precision |
| REAL | double precision |
