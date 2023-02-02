---
title: Oracle
linkTitle: Oracle
headcontent: Known issues when migrating data from Oracle.
description: Refer to the known issues when migrating data using YugabyteDB Voyager and suggested workarounds.
menu:
  preview:
    identifier: oracle-issues
    parent: known-issues
    weight: 101
type: docs
rightNav:
  hideH3: true
---

This page documents known issues you may encounter and suggested workarounds when migrating data from Oracle to YugabyteDB.

## Contents

- [Some numeric types are not exported](#some-numeric-types-are-not-exported)
- [RAW data is not imported in some cases](#raw-data-is-not-imported-in-some-cases)
- [Using a variation of `trunc` with datetime columns in Oracle and YugabyteDB](#using-a-variation-of-trunc-with-datetime-columns-in-oracle-and-yugabytedb)
- [A unique index which is also a primary key is not migrated](#a-unique-index-which-is-also-a-primary-key-is-not-migrated)
- [Issue in some unsupported cases of GIN indexes](#issue-in-some-unsupported-cases-of-gin-indexes)
- [Partition key column not part of primary key columns](#partition-key-column-not-part-of-primary-key-columns)

### Some numeric types are not exported

**GitHub**: [Issue #207](https://github.com/yugabyte/yb-voyager/issues/207)

**Description**: For cases where the precision is less than the scale in a numeric attribute, the numeric attribute fails to get imported to YugabyteDB.

**Workaround**: Manually remove the explicit precision and scale values from the exported numeric or decimal attributes. PostgreSQL and YugabyteDB do not allow setting the precision less than the scale explicitly.

**Example**

An example schema on the source Oracle database is as follows:

```sql
CREATE TABLE numeric_size(
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

### RAW data is not imported in some cases

**GitHub**: [Issue #584](https://github.com/yugabyte/yb-voyager/issues/584)

**Description**: When attempting to migrate a (LONG) RAW attribute from an Oracle instance, you may face an _invalid hexadecimal error_.

**Workaround**: None. A workaround is currently being explored.

---

### Using a variation of `trunc` with datetime columns in Oracle and YugabyteDB

**GitHub**: [Issue #602](https://github.com/yugabyte/yb-voyager/issues/602)

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

### A unique index which is also a primary key is not migrated

**GitHub**: [Issue #571](https://github.com/yugabyte/yb-voyager/issues/571)

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

### Issue in some unsupported cases of GIN indexes

**GitHub**: [Issue #724](https://github.com/yugabyte/yb-voyager/issues/724)

**Description**: If there are some GIN indexes in the schema which are [not supported by YugabyteDB](https://github.com/yugabyte/yugabyte-db/issues/7850), it will display an error during import schema.

**Workaround**: Modify those indexes with some supported cases based on your database configuration.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE members(
    member_id INT GENERATED BY DEFAULT AS IDENTITY,
    first_name VARCHAR2(100) NOT NULL,
    last_name VARCHAR2(100) NOT NULL,
    gender CHAR(1) NOT NULL,
    dob DATE NOT NULL,
    email VARCHAR2(255) NOT NULL,
    PRIMARY KEY(member_id)
);

CREATE BITMAP INDEX members_gender_bm_index on members(gender,member_id);
```

The exported schema is as follows:

```sql
CREATE TABLE members (
    member_id bigint GENERATED BY DEFAULT AS IDENTITY (START WITH 1 INCREMENT BY 1 MAXVALUE 9223372036854775807 MINVALUE 1 NO CYCLE CACHE 20 ),
    first_name varchar(100) NOT NULL,
    last_name varchar(100) NOT NULL,
    gender char(1) NOT NULL,
    dob timestamp NOT NULL,
    email varchar(255) NOT NULL,
    PRIMARY KEY (member_id)
) ;
CREATE INDEX members_gender_bm_index ON members USING gin(gender, member_id);

```

Error when exporting the schema is as follows:

```output
ERROR: data type character has no default operator class for access method "ybgin" (SQLSTATE 42704)
```

---

### Partition key column not part of primary key columns

**GitHub**: [Issue #578](https://github.com/yugabyte/yb-voyager/issues/578)

**Description**:  In YugabyteDB, if a table is partitioned on a column, then that column needs to be a part of the primary key columns. Creating a table where the partition key column is not part of the primary key columns results in an error.

**Workaround**: Add all partition columns to the primary key columns.

**Example**

An example exported schema is as follows:

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
    PRIMARY KEY (employee_id, hire_date)
) PARTITION BY RANGE (hire_date) ;
```
