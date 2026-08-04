---
title: Oracle source database
linkTitle: Oracle
headcontent: What to watch out for when migrating data from Oracle
description: Review limitations and suggested workarounds for migrating data from Oracle.
menu:
  stable_yugabyte-voyager:
    identifier: oracle-issues
    parent: known-issues
    weight: 102
type: docs
---

When migrating data from Oracle to YugabyteDB, you may need to address limitations and incompatibilities by implementing workarounds. Some features, like multi-column partition by list, some DDL operations, and some constraint types, are unsupported. You may also encounter compatibility issues with data types and functions.

The following sections provide guidance on how to adjust your schema, handle unsupported features, and optimize performance for a successful migration.

{{< warning title="Unsupported features">}}
Cluster, Domain, Bitmap join, IOT indexes, and reverse indexes are not exported.
{{< /warning >}}

## Data definition

### Tables

#### Partition key column not part of primary key columns

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

---

#### Tables partitioned with expressions cannot contain primary/unique keys

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

#### Multi-column partition by list is not supported

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

---

### Data types

#### Some numeric types are not exported

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

#### Negative scale is not supported

**GitHub**: [Issue #779](https://github.com/yugabyte/yb-voyager/issues/779)

**Description**: Oracle supports negative scale where you can round down the values to the power of tens corresponding to the scale provided. Negative scale is not supported in PostgreSQL and therefore in YugabyteDB.

**Workaround**: Remove the precision/scale from the exported schema, or change to any other supported datatype.

**Example**

An example source schema is as follows:

```sql
CREATE TABLE num_check (n1 number(5,-2));
```

An example exported schema is as follows:

```sql
CREATE TABLE num_check (n1 decimal(5,-2));
```

An example table with the suggested workaround is as follows:

```sql
CREATE TABLE num_check (n1 decimal);
```

---

#### RAW data is not imported in some cases

**GitHub**: [Issue #584](https://github.com/yugabyte/yb-voyager/issues/584)

**Description**: When attempting to migrate a (LONG) RAW attribute from an Oracle instance, you may face an _invalid hexadecimal error_.

**Workaround**: None. A workaround is currently being explored.

---

#### Large-sized CLOB data is not supported

**GitHub**: [Issue #385](https://github.com/yugabyte/yb-voyager/issues/385)

**Description**: YugabyteDB Voyager ignores any values of CLOB, NCLOB, or BLOB types by default, but for CLOB columns, data export can be enabled using the experimental flag [--allow-oracle-clob-data-export](../../reference/data-migration/export-data/#arguments). However, if the size of rows for such CLOB type columns exceeds 240 MB, it may result in errors and the migration may fail.

**Workaround**: None. A workaround is being currently explored.

---

## Indexes

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

## Migration process and tooling

### Exporting data with names using special characters fails

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

### Issue with importing data from tables with reserved keyword datatypes matching table names

**GitHub**: [Issue #1505](https://github.com/yugabyte/yb-voyager/issues/1505)

**Description**: If there's a table with a name as a reserved word of datatype, then there is an issue in dumping the incorrect data dump for that table, possibly resulting in syntax errors when importing that exported data in target.

**Workaround**: Modify the data to correct syntax as per the datatype exported for that table.

**Example**:

An example schema on the source database is as follows:

```sql
create table "number"(id int PRIMARY KEY);
insert into "number" values(0);
insert into "number" values(1);
insert into "number" values(2);
insert into "number" values(3);
insert into "number" values(4);
```

The exported schema and data are as follows:

```sql
create table "number"(id numeric(38) NOT NULL, PRIMARY KEY (id));

COPY number (id) FROM STDIN;
f
t
2
3
4
\.
```

Error during data import is as follows:

```sql
ERROR: invalid input syntax for type numeric: "t" (SQLSTATE 22P02), COPY number, line 1, column id: "t"
```

Workaround for the example is to modify the data to change the `f/t` to `0/1` respectively:

```sql
COPY number (id) FROM STDIN;
0
1
2
3
4
\.
```

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

---

### Error in CREATE VIEW DDL in synonym.sql

**GitHub**: [Issue #673](https://github.com/yugabyte/yb-voyager/issues/673)

**Description**: When exporting synonyms from Oracle, the CREATE OR REPLACE VIEW DDLs gets exported with full classified name of the object, and while the schema in the DDLs will be same as the schema in which the synonym is present in Oracle, the schema with that name may not be present in the target YugabyteDB database, and so import schema fails with a _does not exist_ error.

**Workaround**: Manual intervention needed. You can resolve the issue with one of the following options:

- Create the target schema with the name mentioned in the object name of DDLs present in `synonym.sql`.
- Remove the schema name from all the object names from the DDLs.

**Example**

An example DDL on the source schema `test` is as follows:

```sql
CREATE OR REPLACE PUBLIC SYNONYM pub_offices for offices;
```

An example exported schema is as follows:

```sql
CREATE OR REPLACE VIEW test.offices AS SELECT * FROM test.locations;
```

Suggested changes to the schema are as follows:

- Execute the following DDL on the target database:

    ```sql
    CREATE SCHEMA test;
    ```

OR

- Modify the DDL by removing the schema name `test` from the DDL:

    ```sql
    CREATE OR REPLACE VIEW offices AS SELECT * FROM locations;
    ```

---

## Server programming

### %TYPE syntax is unsupported

**GitHub**: [Issue #23619](https://github.com/yugabyte/yugabyte-db/issues/23619)

**Description**: In Oracle, the `%TYPE` is a virtual column that is used to declare a variable, column, or parameter with the same data type as an existing database column. This syntax is not supported in target YugabyteDB yet and errors out in import schema as follows:

```output
ERROR: invalid type name "employees.salary%TYPE" (SQLSTATE 42601)
```

**Workaround**: Fix the syntax to include the actual type name instead of referencing the type of a column.

**Example**

An example of exported schema from the source database is as follows:

```sql
CREATE TABLE public.employees (
    employee_id integer NOT NULL,
    employee_name text,
    salary numeric
);


CREATE FUNCTION public.get_employee_salary(emp_id integer) RETURNS numeric
    LANGUAGE plpgsql
    AS $$
DECLARE
    emp_salary employees.salary%TYPE;  -- Declare a variable with the same type as employees.salary
BEGIN
    SELECT salary INTO emp_salary
    FROM employees
    WHERE employee_id = emp_id;

    RETURN emp_salary;
END;
$$;
```

Suggested change to CREATE FUNCTION is as follows:

```sql
CREATE FUNCTION public.get_employee_salary(emp_id integer) RETURNS numeric
    LANGUAGE plpgsql
    AS $$
DECLARE
    Emp_salary NUMERIC;  -- Declare a variable with the same type as employees.salary
BEGIN
    SELECT salary INTO emp_salary
    FROM employees
    WHERE employee_id = emp_id;

    RETURN emp_salary;
END;
$$;
```

---

### TRANSLATE USING is unsupported

**GitHub**: [Issue #1146](https://github.com/yugabyte/yb-voyager/issues/1146)

**Description**: Oracle includes a concept of a National Character Set where it can use the TRANSLATE function to translate a value to the "NCHAR_CS" character set. The significance of the National Character Set is its ability to compare and sort character data based on linguistic rules specific to a particular character set. This function is not supported in PostgreSQL and therefore in YugabyteDB.

**Workaround**: None. For a similar purpose, you can use the `convert` function in YugabyteDB.

**Usage**: convert(string using conversion_name)

**Example**: convert('PostgreSQL' using iso_8859_1_to_utf8)

**Result**: 'PostgreSQL' (in UTF8 (Unicode, 8-bit) encoding)

---

## Performance optimization

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
