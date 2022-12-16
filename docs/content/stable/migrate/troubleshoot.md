---
title: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshoot issues when migrating data using YugabyteDB Voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  stable:
    identifier: troubleshoot-voyager
    parent: voyager
    weight: 106
type: docs
---

This page documents known issues and workarounds, as well as unsupported features when migrating data with YugabyteDB Voyager.

## Known issues

### Issue #573: [MySQL] Unsigned decimal types are not exported properly

**GitHub link**: [Issue #573](https://github.com/yugabyte/yb-voyager/issues/573)

**Description**: Unsigned decimal types lose their precision and have `UNSIGNED` exported along with them.

**Workaround**: Manual intervention needed. Unsigned decimal type is not supported in YugabyteDB. You have to remove `UNSIGNED` and add the precision.

**Example**

An example schema on the source MySQL database is as follows:

```sql
create table if not exists fixed_point_types(
     d_us decimal(10,2)unsigned,
     dec_type dec(5,5) unsigned,
     numeric_type numeric(10,5)unsigned,
     fixed_type fixed(10,3)unsigned
);
```

The exported schema is as follows:

```sql
CREATE TABLE fixed_point_types (
        d_us DECIMAL UNSIGNED,
        dec_type DECIMAL UNSIGNED,
        numeric_type DECIMAL UNSIGNED,
        fixed_type DECIMAL UNSIGNED
) ;
```

Suggested change to the schema is as follows:

```sql
CREATE TABLE fixed_point_types (
        d_us DECIMAL(10,2),
        dec_type DECIMAL(5,5),
        numeric_type DECIMAL(10,5),
        fixed_type DECIMAL (10,3)
) ;
```

---

### Issue #188: [MySQL] Approaching MAX/MIN double precision values are not exported

**GitHub link**: [Issue #188](https://github.com/yugabyte/yb-voyager/issues/188)

**Description**: Exporting double precision values near MAX/MIN value results in an _out of range_ error.

**Workaround**: All float style data types are exported to double precision value in YugabyteDB. You can manually edit post export, or by editing the `DATA_TYPE` directive in the ora2pg base configuration file before starting export.

**Example**

An example schema on the source MySQL database is as follows:

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

Suggested changes to the schema can be done using one of the following options:

- Edit the `/etc/yb-voyager/base-ora2pg.conf` file before exporting schema, using the `DATA_TYPE` directive.

    Default value: DATA_TYPE

    The following table describes the data types and its values:

    | DATA_TYPE | Description |
    |:--------- | :---------- |
    | VARCHAR2/NVARCHAR2 | varchar |
    | DATE/TIMESTAMP | timestamp |
    | LONG/CLOB/NCLOB | text |
    | LONG RAW/BLOB/BFILE/RAW | bytea |
    | UROWI/ROWID | oid |
    | FLOAT/DOUBLE PRECISION/BINARY_FLOAT/BINARY_DOUBLE | double precision |
    | DEC/DECIMAL | decimal |
    | INT/INTEGER | numeric |
    | REAL | real |
    | SMALLINT | smallint |
    | XMLTYPE | xml |
    | BINARY_INTEGER/PLS_INTEGER | integer |
    | TIMESTAMP WITH TIME ZONE/TIMESTAMP WITH LOCAL TIME ZONE | timestamp with time zone |

    Replace the mapping wherever needed, using valid YugabyteDB data types.

- Edit the exported schema files as follows:

    ```sql
    CREATE TABLE floating_point_types (
        float_type double precision, /*can be replaced with any other float-like data type*/
        double_type double precision,
        double_precision_type double precision,
        real_type real
    );
    ```

---

### Issue #579: [MySQL] Functional/Expression indexes fail to migrate

**GitHub link**: [Issue #579](https://github.com/yugabyte/yb-voyager/issues/579)

**Description**: If your schema contains Functional/Expression indexes in MYSQL, the index creation fails with a syntax error during migration and doesn't get migrated.

**Workaround**: Manual intervention needed. You have to remove the back-ticks (``) from the exported schema files.

**Example**

An example schema on the source MySQL database is as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test((year(`to_date`)));
```

The exported schema is as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test ((extract(year from date(`to_date`))));
```

Suggested change to the schema is to remove the back-ticks as follows:

```sql
CREATE INDEX exp_ind ON exp_index_test ((extract(year from date(to_date))));
```

---

### Issue #320: [MySQL] Exporting data from MySQL when table names include quotes

**GitHub link**: [Issue #320](https://github.com/yugabyte/yb-voyager/issues/320)

**Description**: When exporting a schema from MySQL that includes a table name that has quotes, the table is exported with the table name converted to lowercase and without the quotes, resulting in an error.

**Workaround**: Manual intervention needed. You have to rename the table in MySQL and then export and import the data followed by renaming the table in YugabyteDB.

Example tables for source MySQL database is as follows:

```sql
show tables;
```

```output
+----------------------+
| Tables_in_pk_missing |
+----------------------+
| "test_data_COPY"     |
| TEST_DATA            |
+----------------------+
```

The exported schema is as follows:

```sql
CREATE TABLE test_data_copy (
   id serial,
   first_name varchar(50),
   last_name varchar(50),
   email varchar(50),
   gender varchar(50),
   ip_address varchar(20),
   PRIMARY KEY (id)
) ;
```

Error when exporting data is as follows:

```output
DBD::mysql::st execute failed: Table 'pk_missing.test_data_COPY' doesn't exist at /usr/local/share/perl5/Ora2Pg.pm line 14247.
DBD::mysql::st execute failed: Table 'pk_missing.test_data_COPY' doesn't exist at /usr/local/share/perl5/Ora2Pg.pm line 14247.
```

Suggested workaround is as follows:

1. In MySQL, rename the table using a name without quotes using the following command:

    ```sql
    Alter table `"test_data_COPY"` rename test_data_COPY2;
    ```

1. Export and import the data.

1. In YugabyteDB, rename the table to include the quotes using the following command:

    ```sql
    Alter table test_data_copy2 rename to "test_data_COPY";
    ```

---

### Issue #137: [MYSQL] Spatial datatype migration is not yet supported

**GitHub link**: [Issue #137](https://github.com/yugabyte/yb-voyager/issues/137)

**Description**: If your MYSQL schema contains spatial datatypes, the migration will not complete as this migration type is not yet supported by YugabyteDB Voyager. Supporting spatial datatypes will require extra dependencies such as [PostGIS](https://postgis.net/) to be installed.

**Workaround** : None. A workaround is currently being explored.

**Example**

An example schema on the source database is as follows:

CREATE TABLE address (
     address_id int,
     add point,
     location GEOMETRY NOT NULL
);

---

### Issue #207: [Oracle] Some numeric types are not exported

**GitHub link**: [Issue #207](https://github.com/yugabyte/yb-voyager/issues/207)

**Description**: For cases where the precision is less than the scale in a numeric attribute, the numeric attribute fails to get imported to YugabyteDB.

**Workaround**: Manually remove the explicit precision and scale values from the exported numeric or decimal attributes. PostgreSQL and YugabyteDB do not allow setting the precision less than the scale explicitly.

**Example**

An example schema on the source Oracle database is as follows:

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

Suggested change to the schema is as follows:

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

---

### Issue #584: [Oracle] RAW data is not imported in some cases

**GitHub link**: [Issue #584](https://github.com/yugabyte/yb-voyager/issues/584)

**Description**: When attempting to migrate a (LONG) RAW attribute from an Oracle instance, you may face an _invalid hexadecimal error_.

**Workaround**: None. A workaround is currently being explored.

---

### Issue #602: [Oracle] Using a variation of `trunc` with datetime columns in Oracle and YugabyteDB

**GitHub link**: [Issue #602](https://github.com/yugabyte/yb-voyager/issues/602)

**Description**: You can use the `trunc` function with a timestamp column in your Oracle schema, but this variation is not supported in YugabytedB, where the `date_trunc` function is used for these types of datetime columns. When you export such a schema using `trunc`, the data import fails.

**Workaround**: Manual intervention needed. You have to replace `trunc` with `date_trunc` in the exported schema files.

**Example**

An example DDL on the source Oracle database is as follows:

```sql
ALTER TABLE test_timezone ADD CONSTRAINT test_cc1 CHECK ((dtts = trunc(dtts)));
```

Note that the DDL gets exported with `trunc` function and you have to replace it with `date_trunc` after export as follows:

```sql
ALTER TABLE test_timezone ADD CONSTRAINT test_cc1 CHECK ((dtts = date_trunc('day',dtts)));
```

---

### Issue #571: [Oracle] A unique index which is also a primary key is not migrated

**GitHub link**: [Issue #571](https://github.com/yugabyte/yb-voyager/issues/571)

**Description**: If your Oracle schema contains a unique index and a primary key on the same set of columns, the unique index does not get exported.

**Workaround**: Manual intervention needed. You have to manually add the unique index to the exported files.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE employees(employee_id NUMBER(6),email VARCHAR2(25));
CREATE UNIQUE INDEX EMAIL_UNIQUE ON employees (email) ;
ALTER TABLE employees ADD ( CONSTRAINT email_pk PRIMARY KEY (email));
```

Suggested change to the schema is to manually add the unique index to the exported files as follows:

```sql
CREATE UNIQUE INDEX email_unique ON public.employees USING btree (email);
```

---

### Issue #334: [MySQL, Oracle] Importing case-sensitive schema names

**GitHub link**: [Issue #334](https://github.com/yugabyte/yb-voyager/issues/334)

**Description**: When the source is either MySQL or Oracle, if you attempt to migrate the database using a case-sensitive schema name, the migration will fail with `no schema has been selected` or `schema already exists` error(s).

**Workaround**: Currently, yb-voyager does not support migration via case-sensitive schema names; all schema names are assumed to be case-insensitive (lower-case). If necessary, you can alter the schema names to a case-sensitive alternative post-migration using the ALTER SCHEMA command.

**Example**

An example `yb-voyager` command with a case-sensitive schema name is as follows:

```sh
yb-voyager import schema --target-db-name voyager
        --target-db-host localhost
        --export-dir .
        --target-db-password password
        --target-db-user yugabyte
        --target-db-schema "\"Test\""
```

Error during data migration is as follows:

```output
ERROR: no schema has been selected to create in (SQLSTATE 3F000)
```

Suggested changes are as follows:

1. During schema migration, change the case-sensitive schema name as follows:

```sh
yb-voyager import schema --target-db-name voyager
        --target-db-host localhost
        --export-dir .
        --target-db-password password
        --target-db-user yugabyte
        --target-db-schema test
```

1. Post migration, alter the schema name as follows:

```sql
ALTER SCHEMA "test" RENAME TO "Test";
```

---

### Issue #578: [MySQL, Oracle] Partition key column not part of primary key columns

**GitHub link**: [Issue #578](https://github.com/yugabyte/yb-voyager/issues/578)

**Description**:  In YugabyteDB, if a table is partitioned on a column, then that column needs to be a part of the primary key columns. Creating a table where the partition key column is not part of the primary key columns results in an error.

**Workaround**: Add all Partition columns to the Primary key columns.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE employees (
employee_id integer NOT NULL,
first_name varchar(20),
last_name varchar(25),
email varchar(25),
phone_number varchar(20),
hire_date timestamp DEFAULT statement_timestamp(),
job_id varchar(10),
salary double precision,
part_name varchar(25),
PRIMARY KEY (employee_id)) PARTITION BY RANGE (hire_date) ;
```

The preceding example will result in an error as follows:

```output
ERROR:  insufficient columns in the PRIMARY KEY constraint definition
DETAIL:  PRIMARY KEY constraint on table "employees" lacks column "hire_date" which is part of the partition key.
```

An example table with the suggested workaround is as follows:

```sql
CREATE TABLE employees (
employee_id integer NOT NULL,
first_name varchar(20),
last_name varchar(25),
email varchar(25),
phone_number varchar(20),
hire_date timestamp DEFAULT statement_timestamp(),
job_id varchar(10),
salary double precision,
part_name varchar(25),
PRIMARY KEY (employee_id, hire_date)) PARTITION BY RANGE (hire_date) ;
```

---

### Issue #612: [PostgreSQL] Adding primary key to a partitioned table results in an error

**GitHub link**: [Issue #612](https://github.com/yugabyte/yb-voyager/issues/612)

**Description**: If you have a partitioned table in which primary key is added later using `ALTER TABLE`, then the table creation fails with the following error:

```output
ERROR: adding primary key to a partitioned table is not yet implemented (SQLSTATE XX000)
```

**Workaround**: Manual intervention needed. Add primary key in the `CREATE TABLE` statement.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.sales_region (
    id integer NOT NULL,
    amount integer,
    branch text,
    region text NOT NULL
)
PARTITION BY LIST (region);

ALTER TABLE ONLY public.sales_region ADD CONSTRAINT sales_region_pkey PRIMARY KEY (id, region);
```

Suggested change to the schema is as follows:

```sql
CREATE TABLE public.sales_region (
    id integer NOT NULL,
    amount integer,
    branch text,
    region text NOT NULL,
    PRIMARY KEY(id, region)
)
PARTITION BY LIST (region);
```

---

### Issue #49: Index on timestamp column should be imported as ASC (Range) index to avoid sequential scans

**GitHub link**: [Issue #49](https://github.com/yugabyte/yb-voyager/issues/49)

**Description**: If there is an index on a timestamp column, the index should be imported as a range index automatically, as most queries relying on timestamp columns use range predicates. This avoids sequential scans and makes indexed scans accessible.

**Workaround**: Manually add the ASC (range) clause to the exported files.

**Example**

An example schema on the source database is as follows:

```sql
create index ON timestamp_demo (ts);
```

Suggested change to the schema is to add the `asc` clause as follows:

```sql
create index ON timestamp_demo (ts asc);
```

---

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| BLOB and CLOB | yb-voyager currently ignores all columns of type BLOB/CLOB. <br>Use another mechanism to load the attributes.| [43](https://github.com/yugabyte/yb-voyager/issues/43) |
