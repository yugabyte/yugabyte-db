---
title: MySQL and Oracle source databases
linkTitle: MySQL and Oracle
headcontent: General guide when migrating data from MySQL or Oracle.
description: General guide and suggested workarounds for migrating data from MySQL or Oracle.
menu:
  preview_yugabyte-voyager:
    identifier: mysql-oracle-issues
    parent: known-issues
    weight: 103
type: docs
rightNav:
  hideH3: true
---

Review and explore the suggested workarounds for multiple areas when migrating data from MySQL or Oracle to YugabyteDB.

## Contents

- [Tables partitioned with expressions cannot contain primary/unique keys](#tables-partitioned-with-expressions-cannot-contain-primary-unique-keys)
- [Multi-column partition by list is not supported](#multi-column-partition-by-list-is-not-supported)

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
