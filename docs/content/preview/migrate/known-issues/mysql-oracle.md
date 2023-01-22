---
title: MySQL and Oracle
linkTitle: MySQL and Oracle
headcontent: Known issues when migrating data from MySQL or Oracle.
description: Refer to the known issues when migrating data using YugabyteDB Voyager and suggested workarounds.
menu:
  preview:
    identifier: mysql-oracle-issues
    parent: known-issues
    weight: 103
type: docs
rightNav:
  hideH3: true
---

This page documents known issues you may encounter and suggested workarounds when migrating data from MySQL or Oracle to YugabyteDB.

## Contents

- [Importing case-sensitive schema names](#importing-case-sensitive-schema-names)
- [Partition key column not part of primary key columns](#partition-key-column-not-part-of-primary-key-columns)
- [Tables partitioned with expressions cannot contain primary/unique keys](#tables-partitioned-with-expressions-cannot-contain-primary-unique-keys)
- [Multi-column partition by list is not supported](#multi-column-partition-by-list-is-not-supported)

### Importing case-sensitive schema names

**GitHub**: [Issue #334](https://github.com/yugabyte/yb-voyager/issues/334)

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

### Partition key column not part of primary key columns

**GitHub**: [Issue #578](https://github.com/yugabyte/yb-voyager/issues/578)

**Description**:  In YugabyteDB, if a table is partitioned on a column, then that column needs to be a part of the primary key columns. Creating a table where the partition key column is not part of the primary key columns results in an error.

**Workaround**: Add all partition columns to the primary key columns.

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
    PRIMARY KEY (employee_id, hire_date)
) PARTITION BY RANGE (hire_date) ;
```

---

### Tables partitioned with expressions cannot contain primary/unique keys

**GitHub**: [Issue#698](https://github.com/yugabyte/yb-voyager/issues/698)

**Description**: If you have a table in the source database which is partitioned using any expression/function, that table cannot have a primary or unique key on any of its columns, as it is an invalid syntax in YugabyteDB.

**Workaround**: Remove any primary/unique keys from exported schemas.

An example schema on the MySQL source database with primary key is as follows:

```sql
/* Table definition */

CREATE TABLE Sales (
    cust_id INT NOT NULL,
    name VARCHAR(40),
    store_id VARCHAR(20) NOT NULL,
    bill_no INT NOT NULL,
    bill_date DATE NOT NULL,
    amount DECIMAL(8,2) NOT NULL,
    PRIMARY KEY (bill_no,bill_date)
)
PARTITION BY RANGE (year(bill_date))(
    PARTITION p0 VALUES LESS THAN (2016),
    PARTITION p1 VALUES LESS THAN (2017),
    PARTITION p2 VALUES LESS THAN (2018),
    PARTITION p3 VALUES LESS THAN (2020)
);
```

The exported schema is as follows:

```sql
/* Table definition */
CREATE TABLE sales (
    cust_id bigint NOT NULL,
    name varchar(40),
    store_id varchar(20) NOT NULL,
    bill_no bigint NOT NULL,
    bill_date timestamp NOT NULL,
    amount decimal(8,2) NOT NULL,
    PRIMARY KEY (bill_no,bill_date)
) PARTITION BY RANGE ((extract(year from date(bill_date)))) ;
```

Suggested change to the schema is to remove the primary/unique key from the exported schema as follows:

```sql
CREATE TABLE sales (
    cust_id bigint NOT NULL,
    name varchar(40),
    store_id varchar(20) NOT NULL,
    bill_no bigint NOT NULL,
    bill_date timestamp NOT NULL,
    amount decimal(8,2) NOT NULL
) PARTITION BY RANGE ((extract(year from date(bill_date)))) ;
```

---

### Multi-column partition by list is not supported

**GitHub**: [Issue#699](https://github.com/yugabyte/yb-voyager/issues/699)

**Description**: In YugabyteDB, you cannot perform a partition by list on multiple columns and exporting the schema results in an error.

**Workaround**: Make the partition a single column partition by list by making suitable changes or choose other supported partitioning methods.

**Example**

An example schema on the Oracle source database is as follows:

```sql
CREATE TABLE test (
   id NUMBER,
   country_code VARCHAR2(3),
   record_type VARCHAR2(5),
   descriptions VARCHAR2(50),
   CONSTRAINT t1_pk PRIMARY KEY (id)
)
PARTITION BY LIST (country_code, record_type)
(
  PARTITION part_gbr_abc VALUES (('GBR','A'), ('GBR','B'), ('GBR','C')),
  PARTITION part_ire_ab VALUES (('IRE','A'), ('IRE','B')),
  PARTITION part_usa_a VALUES (('USA','A')),
  PARTITION part_others VALUES (DEFAULT)
);
```

The exported schema is as follows:

```sql
CREATE TABLE test (
    id numeric NOT NULL,
    country_code varchar(3),
    record_type varchar(5),
    descriptions varchar(50),
    PRIMARY KEY (id)
) PARTITION BY LIST (country_code, record_type) ;
```

The preceding schema example will result in an error as follows:

```output
ERROR: cannot use "list" partition strategy with more than one column (SQLSTATE 42P17)
```
