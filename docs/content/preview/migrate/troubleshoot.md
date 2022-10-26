---
title: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshoot issues when migrating data using YugabyteDB Voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: troubleshoot-voyager
    parent: voyager
    weight: 106
type: docs
---

This page documents the known issues unsupported features when migrating data with YugabyteDB Voyager.

## Known issues

To troubleshoot problems when migrating data using [yb-voyager](https://github.com/yugabyte/yb-voyager), use the following sections.

| Issue | Source database | Description | Solutions | Examples |
| :---- | :-------------- | :---------- | :-------- | :------- |
| [Unsigned data types are not migrated](https://github.com/yugabyte/yb-voyager/issues/188) | MySQL | Unsigned data types are migrated as signed data types. | Manual intervention needed. Unsigned is not supported in YugabyteDB. You have to allocate extra space for all the unsigned types in their schema. | [Unsigned data types](#unsigned-data-types) |
| [Double precision data type is not exported](https://github.com/yugabyte/yb-voyager/issues/188) | MySQL | Exporting double precision values near max/min value results in an _out of range_ error. | Unable to replicate error. All float style data types are exported to double precision value in YugabyteDB. You can either manually edit the data type post export, or edit the DATA_TYPE directive in the ora2pg base configuration file before starting export. | [Double precision data types](#double-precision-data-types) |
| [Check constraints are not being exported](https://github.com/yugabyte/yb-voyager/issues/242) | MySQL | Check constraints are not exported along with the DDL. | Manual intervention needed. You have to add checks manually to the exported schema files. | [Check constraints](#check-constraints) |
| [Some numeric types do not get exported](https://github.com/yugabyte/yb-voyager/issues/207) | Oracle | In cases where the precision is less than the scale in a numeric attribute, it fails to get imported to YugabyteDB. | Manually remove the explicit precision and scale values from the exported numeric/decimal attributes. PostgreSQL/YugabyteDB does allow explicitly keeping the precision less than the scale. This is currently a fix that works, ora2pg directives need to be explored more to avoid such intervention. | [Export numeric types](#export-numeric-types) |
| [RAW data fails to get imported in some cases](https://github.com/yugabyte/yb-voyager/issues/219) | Oracle | In some cases, you may face _invalid hexadecimal error_ when attempting to migrate a (LONG) RAW attribute from an Oracle instance. | Unable to replicate error. Exploring more currently. | |
| [Functional/Expression indexes fail to migrate](https://github.com/yugabyte/yb-voyager/issues/277) | MySQL | If your schema contains Functional/Expression indexes in MYSQL, during the migration the index creation fails with a syntax error and the index doesn't get migrated. | Manual intervention needed. You have to remove the back-ticks/oblique quotes “”  `  ”” to the exported schema files. | [Migrate Functional/Expression indexes](#migrate-functional-expression-indexes) |
| [Sequences are exported with incorrect restart value 0](https://github.com/yugabyte/yb-voyager/issues/245) | MySQL | Occasionally, when attempting to export a MySQL schema containing AUTO_INCREMENT column types, the exported DDL states an incorrect restart value of 0 pertaining to said column(s). | Manual intervention needed. You have to edit an appropriate restart value into the exported schema DDL corresponding to the auto incremented columns. | [Export sequences](#export-sequences) |
| [Exporting the data from MySQL with table_name including quotes](https://github.com/yugabyte/yb-voyager/issues/320) | MySQL | While exporting the schema from MySQL which consists of the table_name with quotes, it exports the table with table-name converted to lowercase and without quotes, and while exporting the data it throws the error. | Manual intervention needed. You have to rename the table in MySQL and then export and import the whole data, and rename it again in YugabyteDB. | [Quotes in table names](#quotes-in-table-names) |
| [Issue while importing with case-sensitive schema names](https://github.com/yugabyte/yb-voyager/issues/334) | MySQL <br/> Oracle | When the source is either MySQL or Oracle, if you attempt to migrate your database using a case-sensitive schema name, the migration will fail owing to _no schema has been selected_ or _schema already exists_ error(s). |  Currently, yb-voyager does not support migration via case-sensitive schema names; all schema names are assumed to be case-insensitive (lower-case). If needed, you may alter the schema names to a case-sensitive alternative post-migration using the `ALTER SCHEMA` command. | [Case-senstive schema names](#case-sensitive-schema-names) |

### Examples

The following examples include suggested changes to the entries from the [Known issues](#known-issues) table.

#### Unsigned data types

A sample schema on the source database is as follows:

```sql
CREATE TABLE int_types (
    tint_u tinyint unsigned zerofill,
    sint_u smallint unsigned zerofill,
    mint_u mediumint unsigned zerofill,
    int_u int unsigned zerofill,
    bint_u bigint unsigned zerofill
);
```

The exported schema is as follows:

```sql
CREATE TABLE int_types (
    tint_u smallint,
    sint_u smallint,
    mint_u integer,
    int_u integer,
    bint_u bigint
);
```

Suggested change to the schema is as follows:

```sql
CREATE TABLE int_types (
    tint_u smallint,
    sint_u integer,
    mint_u integer,
    int_u bigint,
    bint_u bigint /*can also be converted to numeric(20) if values are too large*/
);
```

#### Double precision data types

A sample schema on the source database is as follows:

```sql
CREATE TABLE floating_point_types(
    float_type float,
    double_type double,
    double_precision_type DOUBLE PRECISION,
    real_type REAL
);
```

The exported schema is as follows:

```sql
CREATE TABLE floating_point_types (
    float_type double precision,
    double_type double precision,
    double_precision_type double precision,
    real_type double precision
);
```

Suggested changes to the schema is as follows:

Option 1: Edit `/etc/yb-voyager/base-ora2pg.conf` before exporting schema, using the `DATA_TYPE` directive.
Default value: DATA_TYPE  VARCHAR2:varchar, NVARCHAR2:varchar, DATE:timestamp, LONG:text, LONG RAW:bytea, CLOB:text, NCLOB:text, BLOB:bytea, BFILE:bytea, RAW:bytea, UROWID:oid, ROWID:oid, FLOAT:double precision, DEC:decimal, DECIMAL:decimal, DOUBLE PRECISION:double precision, INT:numeric, INTEGER:numeric, REAL:real, SMALLINT:smallint, BINARY_FLOAT:double precision, BINARY_DOUBLE:double precision, TIMESTAMP:timestamp, XMLTYPE:xml, BINARY_INTEGER:integer, PLS_INTEGER:integer, TIMESTAMP WITH TIME ZONE:timestamp with time zone,TIMESTAMP WITH LOCAL TIME ZONE:timestamp with time zone
Replace the mapping wherever needed, using valid YugabyteDB data types.

Option 2: Edit the exported schema files as follows:

```sql
CREATE TABLE floating_point_types (
    float_type double precision, /*can be replaced with any other float-like data type*/
    double_type double precision,
    double_precision_type double precision,
    real_type real
);
```

#### Check constraints

A sample schema on the source database is as follows:

```sql
create table check_constraint_test(
    ID int PRIMARY KEY,
    Name varchar(255) NOT NULL,
    Age int,
    City varchar(255),
    CONSTRAINT CHK_CONSTR CHECK (Age>=18 AND City='Bangalore')
);
```

The exported schema is as follows:

```sql
CREATE TABLE check_constraint_test (
    id integer NOT NULL,
    name varchar(255) NOT NULL,
    age integer,
    city varchar(255),
    PRIMARY KEY (id)
);
```

Suggested changes to the schema is as follows:

```sql
CREATE TABLE check_constraint_test (
    id integer NOT NULL,
    name varchar(255) NOT NULL,
    age integer,
    city varchar(255),
    PRIMARY KEY (id)
    CONSTRAINT CHK_CONSTR CHECK (age>=18 AND city='Bangalore')
);
```

#### Export numeric types

A sample schema on the source database is as follows:

```sql
create table numeric_size(
    num_min number(1,-84),
    num_max number(38,127),
    numeric_min numeric(1,-84),
    numeric_max numeric(38,127),
    float_val FLOAT(5),
    dec_min_Val dec(1,-84),
    dec_max_Val dec(38,127),
    decimal_min_Val decimal(1,-84),
    decimal_max_Val decimal(38,127)
);
```

The exported schema is as follows:

```sql
CREATE TABLE numeric_size (
    num_min real,
    num_max decimal(38,127),
    numeric_min real,
    numeric_max decimal(38,127),
    float_val double precision,
    dec_min_val real,
    dec_max_val decimal(38,127),
    decimal_min_val real,
    decimal_max_val decimal(38,127)
) ;
```

Suggested changes to the schema is as follows:

```sql
CREATE TABLE numeric_size (
    num_min real,
    num_max decimal,
    numeric_min real,
    numeric_max decimal,
    float_val double precision,
    dec_min_val real,
    dec_max_val decimal,
    decimal_min_val real,
    decimal_max_val decimal
) ;
```

#### Migrate Functional/Expression indexes

Sample index creation on the source database is as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test((year(`to_date`)));
```

Exported index is as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test ((extract(year from date(`to_date`))));
```

A suggested change is to remove the back-ticks as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test ((extract(year from date(to_date))));
```

#### Export sequences

A sample schema on the source database is as follows:

```sql
CREATE TABLE seq_test (
  id int NOT NULL AUTO_INCREMENT,
  seq_text varchar(10) DEFAULT NULL,
  PRIMARY KEY (id)
);
```

Exported schema is as follows:

```sql
CREATE TABLE seq_test (
    id serial,
    seq_text varchar(10),
    PRIMARY KEY (id)
) ;
ALTER SEQUENCE seq_test_id_seq RESTART WITH 0;
```

Suggested changes to the schema is as follows:

```sql
CREATE TABLE seq_test (
    id serial,
    seq_text varchar(10),
    PRIMARY KEY (id)
) ;
/*Any appropriate restart value greater than 0 may be chosen*/
ALTER SEQUENCE seq_test_id_seq RESTART WITH 2;
```

#### Quotes in table names

A sample schema on the source database is as follows:

```sql
show tables;
```

```output
+----------------------+
| Tables_in_pk_missing |
+----------------------+
| "mock_data_COPY"     |
| MOCK_DATA            |
+----------------------+
```

Exported schema is as follows:

CREATE TABLE mock_data_copy (
   id serial,
   first_name varchar(50),
   last_name varchar(50),
   email varchar(50),
   gender varchar(50),
   ip_address varchar(20),
   PRIMARY KEY (id)
) ;

Error while exporting data is as follows:

```output
DBD::mysql::st execute failed: Table 'pk_missing.mock_data_COPY' doesn't exist at /usr/local/share/perl5/Ora2Pg.pm line 14247.
DBD::mysql::st execute failed: Table 'pk_missing.mock_data_COPY' doesn't exist at /usr/local/share/perl5/Ora2Pg.pm line 14247.
```

Suggested workaround is to rename the table name of table with quotes to a name without quotes in the MySQL database using the following command:

```sql
Alter table `"mock_data_COPY"` rename mock_data_COPY_quotes;
```

Export and import the data and rename the table name in YugabyteDB to the quoted one using the following command:

```sql
Alter table mock_data_copy_quotes rename to "mock_data_COPY";
```

#### Case-sensitive schema names

Sample command with a case-sensitive schema name is as follows:

```sh
yb-voyager import schema --target-db-name voyager --target-db-host localhost --export-dir . --target-db-password password --target-db-user yugabyte --target-db-schema "\"Test\""
```

Error migrating using yb-voyager is as follows:

```output
ERROR: no schema has been selected to create in (SQLSTATE 3F000)
```

Suggested change is to migrate the schema by changing the case sensitive schema name as follows:

```sh
yb-voyager import schema --target-db-name voyager --target-db-host localhost --export-dir . --target-db-password password --target-db-user yugabyte --target-db-schema test
```

Post-migration, alter the schema name as follows:

```sql
ALTER SCHEMA “test” RENAME TO “Test”;
```

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub issue |
| :-------| :---------- | :----------- |
| BLOB and CLOB | yb-voyager currently ignores all columns of type BLOB/CLOB. <br>  Use another mechanism to load the attributes till this feature is supported.| [43](https://github.com/yugabyte/yb-voyager/issues/43) |
| Tablespaces |  Currently YugabyteDB Voyager can't migrate tables associated with certain TABLESPACES automatically. <br> As a workaround, manually create the required tablespace in YugabyteDB and then start the migration.<br> Alternatively if that tablespace is not relevant in the YugabyteDB distributed cluster, you can remove the tablespace association of the table from the create table definition. | [47](https://github.com/yugabyte/yb-voyager/issues/47) |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |

<!-- ## FAQ -->
