---
title: MySQL source database
linkTitle: MySQL
headcontent: What to watch out for when migrating data from MySQL
description: Review limitations and suggested workarounds for migrating data from MySQL.
menu:
  preview_yugabyte-voyager:
    identifier: mysql-issues
    parent: known-issues
    weight: 101
type: docs
rightNav:
  hideH3: true
---

Review limitations and implement suggested workarounds to successfully migrate data from MySQL to YugabyteDB.

## Contents

- [Approaching MAX/MIN double precision values are not imported](#approaching-max-min-double-precision-values-are-not-imported)
- [Functional/Expression indexes fail to migrate](#functional-expression-indexes-fail-to-migrate)
- [Exporting data from MySQL when table names include quotes](#exporting-data-from-mysql-when-table-names-include-quotes)
- [Spatial datatype migration is not yet supported](#spatial-datatype-migration-is-not-yet-supported)
- [Incorrect parsing of views involving functions without alias](#incorrect-parsing-of-views-involving-functions-without-alias)
- [Datatype mismatch in objects causing issues](#datatype-mismatch-in-objects-causing-issues)
- [`drop temporary table` statements are not supported](#drop-temporary-table-statements-are-not-supported)
- [Key defined for a table in functions/procedures cause issues](#key-defined-for-a-table-in-functions-procedures-cause-issues)
- [Multiple declarations of variables in functions](#multiple-declarations-of-variables-in-functions)
- [Exporting text type columns with default value](#exporting-text-type-columns-with-default-value)
- [json_valid() does not exist in PostgreSQL/YugabyteDB](#json-valid-does-not-exist-in-postgresql-yugabytedb)
- [json_value() does not exist in PostgreSQL/YugabyteDB](#json-value-does-not-exist-in-postgresql-yugabytedb)

### Approaching MAX/MIN double precision values are not imported

**GitHub**: [Issue #188](https://github.com/yugabyte/yb-voyager/issues/188)

**Description**: Importing double precision values near MAX/MIN value may result in an _out of range_ error or the exact values may not be imported. This is due to the difference in maximum supported precision values between the two databases. While MySQL supports up to 17 precision values, YugabyteDB supports up to 15.

---

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

### Spatial datatype migration is not yet supported

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

### Datatype mismatch in objects causing issues

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
