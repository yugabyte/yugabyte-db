---
title: MySQL source database
linkTitle: MySQL
headcontent: What to watch out for when migrating data from MySQL
description: Review limitations and suggested workarounds for migrating data from MySQL.
menu:
  stable_yugabyte-voyager:
    identifier: mysql-issues
    parent: known-issues
    weight: 103
type: docs
---

When migrating data from MySQL to YugabyteDB, you may need to address limitations and incompatibilities by implementing workarounds. Some features, like multi-column partition by list, some DDL operations, and some constraint types, are unsupported. You may also encounter compatibility issues with data types and functions.

The following sections provide guidance on how to adjust your schema, handle unsupported features, and optimize performance for a successful migration.

## Data definition

### Tables

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

### Constraints

#### Foreign key referenced column cannot be a subset of a unique/primary key

**GitHub**: [Issue #1608](https://github.com/yugabyte/yb-voyager/issues/1608)

**Description**: In MySQL, you can reference a column which is part of a unique key or a primary key for a foreign key. This is not allowed in YugabyteDB or PostgreSQL.

**Workaround**: The referenced columns will need to have individual Unique / Primary keys.

**Example**:

An example schema on the source database (allowed in MySQL) is as follows:

```sql
create table k(id int,id2 int,UNIQUE KEY `uk_k` (`id`,`id2`));

create table h(id int,CONSTRAINT `fk_h` FOREIGN KEY (`id`) REFERENCES `k` (`id`));
```

An example of the exported schema is as follows:

```sql
CREATE TABLE h (
        id bigint
) ;
CREATE TABLE k (
        id bigint,
        id2 bigint
) ;
ALTER TABLE k ADD UNIQUE (id,id2);
ALTER TABLE h ADD CONSTRAINT fk_h FOREIGN KEY (id) REFERENCES k(id) MATCH SIMPLE ON DELETE NO ACTION ON UPDATE NO ACTION;
```

Error during import is as follows:

```output
ERROR:  there is no unique constraint matching given keys for referenced table "k"
```

Suggested change to the schema is to add individual unique/primary keys to the referenced column as follows:

```sql
ALTER TABLE k ADD UNIQUE (id);
```

---

### Data types

#### Spatial datatype migration is not yet supported

**GitHub**: [Issue #137](https://github.com/yugabyte/yb-voyager/issues/137)

**Description**: If your MYSQL schema contains spatial data types, the migration will not complete as this migration type is not yet supported by YugabyteDB Voyager. Supporting spatial data types will require extra dependencies such as [PostGIS](https://postgis.net/) to be installed.

**Workaround** : None. A workaround is currently being explored.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE address (
  address_id int,
  add point,
  location GEOMETRY NOT NULL
);
```

---

#### DOUBLE UNSIGNED and FLOAT UNSIGNED data types are not supported

**GitHub**: [Issue #1607](https://github.com/yugabyte/yb-voyager/issues/1607)

**Description**: If the schema has a table with a DOUBLE UNSIGNED or FLOAT UNSIGNED column, those data types are not converted by Voyager to a YugabyteDB relevant syntax, and result in errors during import. These data types are deprecated as of [MySQL 8.0.17](https://dev.mysql.com/doc/refman/8.0/en/numeric-type-syntax.html).

**Workaround**: Manually change the exported schema to the closest YugabyteDB supported syntax.

**Example**:

An example schema on the source database is as follows:

```sql
CREATE TABLE test(id int,
                  n double unsigned,
                  f float unsigned
);
```

An example of the exported table is as follows:

```sql
CREATE TABLE test (
        id bigint,
        n DOUBLE UNSIGNED,
        f FLOAT UNSIGNED
);
```

Suggested change to the schema is to change to YugabyteDB-compatible syntax closest to the respective data types:

```sql
CREATE TABLE test (
        id bigint,
        n DOUBLE PRECISION CHECK (n >= 0),
        f REAL CHECK (f >= 0)
);
```

---

#### Approaching MAX/MIN double precision values are not imported

**GitHub**: [Issue #188](https://github.com/yugabyte/yb-voyager/issues/188)

**Description**: Importing double precision values near MAX/MIN value may result in an _out of range_ error or the exact values may not be imported. This is due to the difference in maximum supported precision values between the two databases. While MySQL supports up to 17 precision values, YugabyteDB supports up to 15.

---

#### Datatype mismatch in objects causing issues

**GitHub**: [Issue #690](https://github.com/yugabyte/yb-voyager/issues/690)

**Description**: If you have an object which references a table column whose datatype has been mapped to something else, there may be datatype mismatch issues on the target YugabyteDB database.

**Workaround**: Type cast the reference to match the table column.

**Example**

In the following example, the table column type `int` is mapped to `bigint` and causes issues when referenced in the view via the function, because the function parameter remains as `int` type.

```sql
/* Table definition */
DROP TABLE IF EXISTS bar;
CREATE TABLE bar(
id int,
p_name varchar(10)
);

/* Function definition */
DROP FUNCTION IF EXISTS foo;
delimiter //
CREATE FUNCTION foo (p_id int)
RETURNS varchar(20)
READS SQL DATA
  BEGIN
    RETURN (SELECT p_name FROM bar WHERE p_id=id);
  END//
delimiter ;

/* View definition */
CREATE OR REPLACE VIEW v2 AS SELECT foo(id) AS p_name FROM bar;
```

The exported schema is as follows:

```sql
/* Table definition */
CREATE TABLE bar(
id bigint,
p_name varchar(10)
);

/* Function definition */
CREATE OR REPLACE FUNCTION foo (p_id integer) RETURNS varchar AS $body$
  BEGIN
    RETURN (SELECT p_name FROM bar WHERE p_id=id);
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;

/* View definition */
CREATE OR REPLACE VIEW v1 AS SELECT foo(bar.id) AS p_name FROM bar;
```

Suggested change is to type cast the reference to match the table column as follows:

```sql
CREATE OR REPLACE VIEW v1 AS SELECT foo(bar.id::int) AS p_name FROM bar;
```

---

## Indexes

### Functional/Expression indexes fail to migrate

**GitHub**: [Issue #579](https://github.com/yugabyte/yb-voyager/issues/579)

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

### Unnecessary DDLs for RANGE COLUMN PARTITIONED tables

**GitHub**: [Issue #511](https://github.com/yugabyte/yb-voyager/issues/511)

**Description**: If you have a schema which contains RANGE COLUMN PARTITIONED tables in MYSQL, some extra indexes are created during migration which are unnecessary and might result in an import error.

**Workaround**: Remove the unnecessary DDLs from `PARTITION_INDEXES_partition.sql` file in the `export-dir/schema/partitions` sub-directory for the corresponding tables.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE range_columns_partition_test (
a INT,
b INT
)
PARTITION BY RANGE COLUMNS(a, b) (
PARTITION p0 VALUES LESS THAN (5, 5),
PARTITION p1 VALUES LESS THAN (MAXVALUE, MAXVALUE)
);
```

An example of the exported unnecessary index is as follows:

```sql
-- Create indexes on each partition of table range_columns_partition_test
CREATE INDEX range_columns_partition_test_p1_b ON range_columns_partition_test_p1 (b);
```

Suggested change is to remove the unnecessary index.

---

### Multiple indexes on the same column of a table errors during import

**GitHub**: [Issue #1609](https://github.com/yugabyte/yb-voyager/issues/1609)

**Description**: Voyager renames the indexes with a set pattern `table_name_column_name` during export. So having multiple indexes on the same column of a table will have the same name and will error out during import.

**Workarounds**:

1. Rename the duplicate index after the schema export.
1. Drop the duplicate index on the source.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE test(id int, k int);
CREATE INDEX index1 on test(k);
CREATE INDEX index2 on test(k);
```

An example of the exported indexes are as follows:

```sql
CREATE INDEX test_k ON test (k);
CREATE INDEX test_k ON test (k);
```

Suggested changes:

1. Rename one of the indexes (for example, test_k2).
1. Drop index1 or index2 on the source before exporting.

---

## Migration process and tooling

### Exporting data from MySQL when table names include quotes

**GitHub**: [Issue #320](https://github.com/yugabyte/yb-voyager/issues/320)

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

### Exporting data with names using special characters fails

**GitHub**: [Issue #636](https://github.com/yugabyte/yb-voyager/issues/636), [Issue #688](https://github.com/yugabyte/yb-voyager/issues/688), [Issue #702](https://github.com/yugabyte/yb-voyager/issues/702)

**Description**: If you define complex names for your source database tables, functions, or procedures using backticks or double quotes (for example, \`abc xyz\` , \`abc@xyz\`, or "abc@123"), the migration hangs during the export data step.

**Workaround**: Rename the objects on the source database to a name without special characters.

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

### Incorrect parsing of views involving functions without alias

**GitHub**: [Issue #689](https://github.com/yugabyte/yb-voyager/issues/689)

**Description**: If a view contains a function in its definition in a SELECT statement without any ALIAS, an alias corresponding to the function is appended to the schema. This results in an invalid schema.

**Workaround**: Remove or change the alias to a valid value.

**Example**

An example schema on the source database is as follows:

```sql
CREATE OR REPLACE VIEW v1 AS SELECT foo(id) FROM bar;
```

The exported schema is as follows:

```sql
CREATE OR REPLACE VIEW v1 AS SELECT foo(bar.id) AS foo(id) FROM bar;
```

Choose one from the following suggested changes to the schema.

- Remove the alias as follows:

    ```sql
    CREATE OR REPLACE VIEW v1 AS SELECT foo(bar.id) FROM bar;
    ```

- Change the alias as follows:

    ```sql
    CREATE OR REPLACE VIEW v1 AS SELECT foo(bar.id) AS foo FROM bar;
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

The import will fail with the following error:

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

### Exporting text type columns with default value

**GitHub**: [Issue #621](https://github.com/yugabyte/yb-voyager/issues/621)

**Description**: If you have a default value for text type columns in MYSQL, it does not export properly and fails during import.

**Workaround**: Manually remove the extra encoding DDLs from the exported files.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE text_types (
    id int,
    tt TINYTEXT DEFAULT ('c'),
    te TEXT DEFAULT ('abc'),
    mt MEDIUMTEXT DEFAULT ('abc'),
    lt LONGTEXT DEFAULT ('abc')
);
```

The exported schema is as follows:

```sql
CREATE TABLE text_types (
    id bigint,
    tt text DEFAULT _utf8mb4\'c\',
    te text DEFAULT _utf8mb4\'abc\',
    mt text DEFAULT _utf8mb4\'abc\',
    lt text DEFAULT _utf8mb4\'abc\'
) ;
```

Suggested changes to the schema is to remove the encoding as follows:

```sql
CREATE TABLE text_types (
    id bigint,
    tt text DEFAULT 'c',
    te text DEFAULT 'abc',
    mt text DEFAULT 'abc',
    lt text DEFAULT 'abc'
) ;
```

---

## Server programming

### `drop temporary table` statements are not supported

**GitHub**: [Issue #705](https://github.com/yugabyte/yb-voyager/issues/705)

**Description**: If you have a temporary table defined in a function in MySQL and you have a `drop temporary table` statement associated with it, the schema gets exported as is, which is an invalid syntax in YugabyteDB.

**Workaround**: Manually remove the temporary clause from the drop statement.

**Example**

An example schema on the source database is as follows:

```sql
/* function definition */
delimiter //
CREATE FUNCTION func (p_id int)
RETURNS VARCHAR(20)
READS SQL DATA
  BEGIN
    DROP TEMPORARY TABLE IF EXISTS temp;
    CREATE TEMPORARY TABLE temp(id int, name text);
    INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
    RETURN (SELECT name FROM temp);
END//
delimiter;
```

The exported schema is as follows:

```sql
CREATE OR REPLACE FUNCTION func (p_id integer) RETURNS varchar AS $body$
  BEGIN
    DROP TEMPORARY TABLE IF EXISTS temp;
    CREATE TEMPORARY TABLE temp(id int, name text);
    INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
    RETURN (SELECT name FROM temp);
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;
```

Suggested change to the schema is to remove the temporary clause from the drop statement as follows:

```sql
CREATE OR REPLACE FUNCTION func (p_id integer) RETURNS varchar AS $body$
  BEGIN
    DROP TABLE IF EXISTS temp;
    CREATE TEMPORARY TABLE temp(id int, name text);
    INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
    RETURN (SELECT name FROM temp);
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;
```

---

### json_valid() does not exist in PostgreSQL/YugabyteDB

**GitHub**: [Issue #833](https://github.com/yugabyte/yb-voyager/issues/833)

**Description**: The MYSQL function `json_valid()` which returns 0 or 1 to indicate whether a value is valid JSON, does not exist in PostgreSQL or YugabyteDB.

**Workaround**: Manually create the function on the target.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE test(id int, address json);
ALTER TABLE test ADD CONSTRAINT add_ck CHECK ((json_valid(address)));
```

The contents of schema/failed.sql is as follows:

```sql
ALTER TABLE test ADD CONSTRAINT add_ck CHECK ((json_valid(address)));
```

Suggested solution is as follows:

1. Add the following function to the file "schema/functions/functions.sql" as follows:

    ```sql
    CREATE OR REPLACE FUNCTION json_valid(p_json text)
      RETURNS boolean
    AS
    $$
    BEGIN
      RETURN (p_json::json is not null);
    EXCEPTION
      WHEN OTHERS THEN
        RETURN false;
    END;
    $$
    LANGUAGE PLPGSQL
    IMMUTABLE;
    ```

1. Create the preceding function manually on the target before importing the schema.

---

### json_value() does not exist in PostgreSQL/YugabyteDB

**GitHub**: [Issue #834](https://github.com/yugabyte/yb-voyager/issues/834)

**Description**: The MySQL function `json_value()` which extracts scalar value at the specified path from the given JSON document and returns it as the specified type, does not exist in PostgreSQL or YugabyteDB.

**Workaround**: Use the alternative function `json_extract_path_text()`.

**Example**

An example schema on the source database is as follows:

```sql
json_value(key_value_pair_variable, '$.key');
```

The exported schema is as follows:

```sql
json_value(key_value_pair_variable, '$.key');
```

Suggested change to the schema is as follows:

```sql
json_extract_path_text(key_value_pair_variable::json,'key');
```

---

### Multiple declarations of variables in functions

**GitHub**: [Issue #708](https://github.com/yugabyte/yb-voyager/issues/708)

**Description**: If you re-initializate a variable in a function in MySQL using the set statement, the variable is declared twice with different data types in the exported schema.

**Workaround**: Manually remove the extra declaration of the variable from the exported schema file.

**Example**

An example declaration of the variable in the schema is as follows:

```sql
/* function definition */
delimiter //
CREATE FUNCTION xyz()
RETURNS VARCHAR(10)
READS SQL DATA
  BEGIN
    DECLARE max_date date;
    SET max_date=(SELECT CURRENT_DATE());
    SET @max_date=max_date;
    RETURN max_date;
  END //
delimiter;
```

The exported schema is as follows:

```sql
CREATE OR REPLACE FUNCTION xyz () RETURNS varchar AS $body$
DECLARE
max_date timestamp;max_date date;
  BEGIN
    max_date = (SELECT CURRENT_DATE
    );
    max_date:=max_date;
    RETURN max_date;
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;
```

Suggested change to the schema is to remove the extra declaration of the variable as follows:

```sql
CREATE OR REPLACE FUNCTION xyz () RETURNS varchar AS $body$
DECLARE
max_date timestamp;
  BEGIN
    max_date = (SELECT CURRENT_DATE
    );
    max_date:=max_date;
    RETURN max_date;
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;
```

---

### Key defined for a table in functions/procedures cause issues

**GitHub**: [Issue #707](https://github.com/yugabyte/yb-voyager/issues/707)

**Description**: If you have a basic _key_ defined for a table in a function/procedure, the schema is exported as is, and causes issues because using a key in YugabyteDB is an invalid syntax.

**Workaround**: Manually remove the key from the exported schema, or create an index.

**Example**

An example schema on the source database is as follows:

```sql
/* function definition */

delimiter //
CREATE FUNCTION foo (p_id int)
RETURNS varchar(20)
READS SQL DATA
  BEGIN
    CREATE TEMPORARY TABLE temp(id int, name text,key(id));
    INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
    RETURN (SELECT name FROM temp);
  END//
```

The exported schema is as follows:

```sql
CREATE OR REPLACE FUNCTION foo (p_id integer) RETURNS varchar AS $body$
  BEGIN
    CREATE TEMPORARY TABLE temp(id int, name text,key(id));
    INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
    RETURN (SELECT name FROM temp);
  END;
$body$
LANGUAGE PLPGSQL
SECURITY DEFINER
;
```

Choose one from the following suggested changes to the schema.

- Remove the key from the schema as follows:

    ```sql
    CREATE OR REPLACE FUNCTION foo (p_id integer) RETURNS varchar AS $body$
      BEGIN
        CREATE TEMPORARY TABLE temp(id int, name text);
        INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
        RETURN (SELECT name FROM temp);
      END;
    $body$
    LANGUAGE PLPGSQL
    SECURITY DEFINER
    ;
    ```

- Create an index manually as follows:

    ```sql
    CREATE OR REPLACE FUNCTION foo (p_id integer) RETURNS varchar AS $body$
      BEGIN
        CREATE TEMPORARY TABLE temp(id int, name text);
        CREATE INDEX index_as_key ON temp(id);
        INSERT INTO temp(id,name) SELECT id,p_name FROM bar WHERE p_id=id;
        RETURN (SELECT name FROM temp);
      END;
    $body$
    LANGUAGE PLPGSQL
    SECURITY DEFINER
    ;
    ```

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
