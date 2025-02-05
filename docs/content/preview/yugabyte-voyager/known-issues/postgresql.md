---
title: PostgreSQL source database
linkTitle: PostgreSQL
headcontent: What to watch out for when migrating data from PostgreSQL
description: Review limitations and suggested workarounds for migrating data from PostgreSQL.
menu:
  preview_yugabyte-voyager:
    identifier: postgresql-issues
    parent: known-issues
    weight: 101
type: docs
rightNav:
  hideH3: true
---

Review limitations and implement suggested workarounds to successfully migrate data from PostgreSQL to YugabyteDB.

## Contents

- [Adding primary key to a partitioned table results in an error](#adding-primary-key-to-a-partitioned-table-results-in-an-error)
- [Index creation on partitions fail for some YugabyteDB builds](#index-creation-on-partitions-fail-for-some-yugabytedb-builds)
- [Creation of certain views in the rule.sql file](#creation-of-certain-views-in-the-rule-sql-file)
- [Create or alter conversion is not supported](#create-or-alter-conversion-is-not-supported)
- [GENERATED ALWAYS AS STORED type column is not supported](#generated-always-as-stored-type-column-is-not-supported)
- [Unsupported ALTER TABLE DDL variants in source schema](#unsupported-alter-table-ddl-variants-in-source-schema)
- [Storage parameters on indexes or constraints in the source PostgreSQL](#storage-parameters-on-indexes-or-constraints-in-the-source-postgresql)
- [Foreign table in the source database requires SERVER and USER MAPPING](#foreign-table-in-the-source-database-requires-server-and-user-mapping)
- [Exclusion constraints is not supported](#exclusion-constraints-is-not-supported)
- [PostgreSQL extensions are not supported by target YugabyteDB](#postgresql-extensions-are-not-supported-by-target-yugabytedb)
- [Deferrable constraint on constraints other than foreign keys is not supported](#deferrable-constraint-on-constraints-other-than-foreign-keys-is-not-supported)
- [Data ingestion on XML data type is not supported](#data-ingestion-on-xml-data-type-is-not-supported)
- [GiST, BRIN, and SPGIST index types are not supported](#gist-brin-and-spgist-index-types-are-not-supported)
- [Indexes on some complex data types are not supported](#indexes-on-some-complex-data-types-are-not-supported)
- [Constraint trigger is not supported](#constraint-trigger-is-not-supported)
- [Table inheritance is not supported](#table-inheritance-is-not-supported)
- [%Type syntax is not supported](#type-syntax-is-not-supported)
- [GIN indexes on multiple columns are not supported](#gin-indexes-on-multiple-columns-are-not-supported)
- [Policies on users in source require manual user creation](#policies-on-users-in-source-require-manual-user-creation)
- [VIEW WITH CHECK OPTION is not supported](#view-with-check-option-is-not-supported)
- [UNLOGGED table is not supported](#unlogged-table-is-not-supported)
- [Index on timestamp column should be imported as ASC (Range) index to avoid sequential scans](#index-on-timestamp-column-should-be-imported-as-asc-range-index-to-avoid-sequential-scans)
- [Exporting data with names for tables/functions/procedures using special characters/whitespaces fails](#exporting-data-with-names-for-tables-functions-procedures-using-special-characters-whitespaces-fails)
- [Importing with case-sensitive schema names](#importing-with-case-sensitive-schema-names)
- [Unsupported datatypes by YugabyteDB](#unsupported-datatypes-by-yugabytedb)
- [Unsupported datatypes by Voyager during live migration](#unsupported-datatypes-by-voyager-during-live-migration)
- [XID functions is not supported](#xid-functions-is-not-supported)
- [REFERENCING clause for triggers](#referencing-clause-for-triggers)
- [BEFORE ROW triggers on partitioned tables](#before-row-triggers-on-partitioned-tables)
- [Advisory locks is not yet implemented](#advisory-locks-is-not-yet-implemented)
- [System columns is not yet supported](#system-columns-is-not-yet-supported)
- [XML functions is not yet supported](#xml-functions-is-not-yet-supported)
- [Large Objects and its functions are currently not supported](#large-objects-and-its-functions-are-currently-not-supported)
- [PostgreSQL 12 and later features](#postgresql-12-and-later-features)
- [MERGE command](#merge-command)
- [JSONB subscripting](#jsonb-subscripting)
- [Events Listen / Notify](#events-listen-notify)
- [Two-Phase Commit](#two-phase-commit)
- [DDL operations within the Transaction](#ddl-operations-within-the-transaction)

### Adding primary key to a partitioned table results in an error

**GitHub**: [Issue #612](https://github.com/yugabyte/yb-voyager/issues/612)

**Description**: If you have a partitioned table in which primary key is added later using `ALTER TABLE`, then the table creation fails with the following error:

```output
ERROR: adding primary key to a partitioned table is not yet implemented (SQLSTATE XX000)
```

**Workaround**: Manual intervention needed. Add primary key in the `CREATE TABLE` statement.

**Fixed In**: {{<release "2024.1.0.0, 2024.2.0.0, 2.23.0.0, 2.25">}}.

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

### Index creation on partitions fail for some YugabyteDB builds

**GitHub**: [Issue #14529](https://github.com/yugabyte/yugabyte-db/issues/14529)

**Description**: If you have a partitioned table with indexes on it, the migration will fail with an error for YugabyteDB `2.15` or `2.16` due to a regression.

Note that this is fixed in release [2.17.1.0](../../../releases/ybdb-releases/v2.17/#v2.17.1.0).

**Workaround**: N/A

**Example**

An example schema on the source database is as follows:

```sql
DROP TABLE IF EXISTS list_part;

CREATE TABLE list_part (id INTEGER, status TEXT, arr NUMERIC) PARTITION BY LIST(status);

CREATE TABLE list_active PARTITION OF list_part FOR VALUES IN ('ACTIVE');

CREATE TABLE list_archived PARTITION OF list_part FOR VALUES IN ('EXPIRED');

CREATE TABLE list_others PARTITION OF list_part DEFAULT;

INSERT INTO list_part VALUES (1,'ACTIVE',100), (2,'RECURRING',20), (3,'EXPIRED',38), (4,'REACTIVATED',144), (5,'ACTIVE',50);

CREATE INDEX list_ind ON list_part(status);
```

---

### Creation of certain views in the rule.sql file

**GitHub**: [Issue #770](https://github.com/yugabyte/yb-voyager/issues/770)

**Description**: There may be few cases where certain exported views come under the `rule.sql` file and the `view.sql` file might contain a dummy view definition. This `pg_dump` behaviour may be due to how PostgreSQL handles views internally (via rules).

{{< note title ="Note" >}}
This does not affect the migration as YugabyteDB Voyager takes care of the DDL creation sequence internally.
{{< /note >}}

**Workaround**: Not required

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE foo(n1 int PRIMARY KEY, n2 int);
CREATE VIEW v1 AS
  SELECT n1,n2
  FROM foo
  GROUP BY n1;
```

The exported schema for `view.sql` is as follows:

```sql
CREATE VIEW public.v1 AS
  SELECT
    NULL::integer AS n1,
    NULL::integer AS n2;
```

The exported schema for `rule.sql` is as follows:

```sql
CREATE OR REPLACE VIEW public.v1 AS
  SELECT foo.n1,foo.n2
  FROM public.foo
  GROUP BY foo.n1;
```

### Create or alter conversion is not supported

**GitHub**: [Issue #10866](https://github.com/yugabyte/yugabyte-db/issues/10866)

**Description**: If you have conversions in your PostgreSQL database, they will error out as follows as conversions are currently not supported in the target YugabyteDB:

```output
ERROR:  CREATE CONVERSION not supported yet
```

**Workaround**: Remove the conversions from the exported schema and modify the applications to not use these conversions before pointing them to YugabyteDB.

**Example**

An example schema on the source database is as follows:

```sql
CREATE CONVERSION public.my_latin1_to_utf8 FOR 'LATIN1' TO 'UTF8' FROM public.latin1_to_utf8;

CREATE FUNCTION public.latin1_to_utf8(src_encoding integer, dest_encoding integer, src bytea, dest bytea, len integer) RETURNS integer
    LANGUAGE c
    AS '/usr/lib/postgresql/12/lib/latin1_to_utf8.so', 'my_latin1_to_utf8';
```

---

### GENERATED ALWAYS AS STORED type column is not supported

**GitHub**: [Issue #10695](https://github.com/yugabyte/yugabyte-db/issues/10695)

**Description**: If you have tables in the source database with columns of GENERATED ALWAYS AS STORED type (which means the data of this column is derived from some other columns of the table), it will throw a syntax error in YugabyteDB as follows:

```output
ERROR: syntax error at or near "(" (SQLSTATE 42601)
```

**Workaround**: Create a trigger on this table that updates its value on any INSERT/UPDATE operation, and set a default value for this column. This provides functionality similar to PostgreSQL's GENERATED ALWAYS AS STORED columns using a trigger.

**Fixed In**: {{<release "2.25">}}.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE people (
    name        text,
    height_cm   numeric,
    height_in   numeric GENERATED ALWAYS AS (height_cm / 2.54) STORED
);
```

Suggested change to the schema is as follows:

```sql
ALTER TABLE people
    ALTER COLUMN height_in SET DEFAULT -1;

CREATE OR REPLACE FUNCTION compute_height_in() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.height_in IS DISTINCT FROM -1 THEN
        RAISE EXCEPTION 'cannot insert in column "height_in"';
    ELSE
        NEW.height_in := NEW.height_cm / 2.54;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER compute_height_in_trigger
    BEFORE INSERT OR UPDATE ON people
    FOR EACH ROW
    EXECUTE FUNCTION compute_height_in();
```

---

### Unsupported ALTER TABLE DDL variants in source schema

**GitHub**: [Issue #1124](https://github.com/yugabyte/yugabyte-db/issues/1124)

**Description**: If you have made the following alterations on the source schema, they will come up in the exported schema and are not supported by the target YugabyteDB:

1. `ALTER TABLE ONLY table_name ALTER COLUMN column_name SET (prop = value …)`
1. `ALTER TABLE table_name DISABLE RULE rule_name;`
1. `ALTER TABLE table_name CLUSTER ON index_name;`

```output
ERROR: ALTER TABLE ALTER column not supported yet (SQLSTATE 0A000)
ERROR: ALTER TABLE DISABLE RULE not supported yet (SQLSTATE 0A000)
ERROR: ALTER TABLE CLUSTER not supported yet (SQLSTATE 0A000)
```

**Workaround**: For (1) and (3), remove the alterations from the exported schema. For (2), remove the ALTER and the respective RULE as well so that it is not enabled on the table.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.example (
    name    text,
    email   text,
    new_id  integer NOT NULL,
    id2     integer NOT NULL,
    CONSTRAINT example_name_check CHECK ((char_length(name) > 3))
)
WITH (fillfactor = 70);

ALTER TABLE ONLY public.example
    ALTER COLUMN name SET (n_distinct = 0.1);

CREATE RULE example_rule AS
    ON INSERT TO public.example
    DO NOTIFY example_channel;

ALTER TABLE public.example
    DISABLE RULE example_rule;

CREATE INDEX example_name_idx
    ON public.example USING btree (name);

ALTER TABLE public.example
    CLUSTER ON example_name_idx;
```

---

### Storage parameters on indexes or constraints in the source PostgreSQL

**GitHub**: [Issue #23467](https://github.com/yugabyte/yugabyte-db/issues/23467)

**Description**: If you have storage parameters in objects like tables, indexes, and constraints in the source database, those are also exported with schema but YugabyteDB doesn't support these storage parameters and will error out as follows:

```output
ERROR: unrecognized parameter "<storage_parameter>" (SQLSTATE 22023)
```

**Workaround**: Remove the parameter(s) from the DDL.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.example (
    name         text,
    email        text,
    new_id       integer NOT NULL,
    id2          integer NOT NULL,
    CONSTRAINT example_name_check CHECK ((char_length(name) > 3))
);

ALTER TABLE ONLY public.example
    ADD CONSTRAINT example_email_key UNIQUE (email) WITH (fillfactor = 70);

CREATE INDEX abc
    ON public.example USING btree (new_id) WITH (fillfactor = 70);
```

Suggested change to schema is as follows:

```sql
CREATE TABLE public.example (
    name        text,
    email       text,
    new_id      integer NOT NULL,
    id2         integer NOT NULL,
    CONSTRAINT example_name_check CHECK ((char_length(name) > 3))
);

ALTER TABLE ONLY public.example
    ADD CONSTRAINT example_email_key UNIQUE (email);

CREATE INDEX abc
    ON public.example USING btree (new_id);
```

---

### Foreign table in the source database requires SERVER and USER MAPPING

**GitHub**: [Issue #1627](https://github.com/yugabyte/yb-voyager/issues/1627)

**Description**: If you have foreign tables in the schema, during the export schema phase the exported schema does not include the SERVER and USER MAPPING objects. You must manually create these objects before importing schema, otherwise FOREIGN TABLE creation fails with the following error:

```output
ERROR: server "remote_server" does not exist (SQLSTATE 42704)
```

**Workaround**: Create the SERVER and its USER MAPPING manually on the target YugabyteDB database.

**Example**

An example schema on the source database is as follows:

```sql
CREATE EXTENSION postgres_fdw;

CREATE SERVER remote_server
    FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (host '127.0.0.1', port '5432', dbname 'postgres');

CREATE FOREIGN TABLE foreign_table (
    id    INT,
    name  TEXT,
    data  JSONB
)
SERVER remote_server
OPTIONS (
    schema_name 'public',
    table_name 'remote_table'
);

CREATE USER MAPPING FOR postgres
SERVER remote_server
OPTIONS (user 'postgres', password 'XXX');
```

Exported schema only has the following:

```sql
CREATE FOREIGN TABLE foreign_table (
    id    INT,
    name  TEXT,
    data  JSONB
)
SERVER remote_server
OPTIONS (
    schema_name 'public',
    table_name 'remote_table'
);
```

Suggested change is to manually create the SERVER and USER MAPPING on the target YugabyteDB.

---

### Exclusion constraints is not supported

**GitHub**: [Issue #3944](https://github.com/yugabyte/yugabyte-db/issues/3944)

**Description**: If you have exclusion constraints on the tables in the source database, those will error out during import schema to the target with the following error:

```output
ERROR: EXCLUDE constraint not supported yet (SQLSTATE 0A000)
```

**Workaround**: To implement exclusion constraints, follow this workaround:

1. Create a trigger: Set up a TRIGGER for INSERT or UPDATE operations on the table. This trigger will use the specified expression to search the relevant columns for any potential violations.

1. Add indexes: Create an INDEX on the columns involved in the expression. This helps ensure that the search operation performed by the trigger does not negatively impact performance.

Note that creating an index on the relevant columns _is essential_ for maintaining performance. Without an index, the trigger's search operation can degrade performance.

**Caveats**: Note that there are specific issues related to creating indexes on certain data types using certain index methods in YugabyteDB. Depending on the data types or methods involved, additional workarounds may be required to ensure optimal performance for these constraints.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.meeting (
    id          integer NOT NULL,
    room_id     integer NOT NULL,
    time_range  tsrange NOT NULL
);

ALTER TABLE ONLY public.meeting
    ADD CONSTRAINT no_time_overlap EXCLUDE USING gist (room_id WITH =, time_range WITH &&);
```

Suggested change to schema is as follows:

```sql
CREATE OR REPLACE FUNCTION check_no_time_overlap() RETURNS TRIGGER AS $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM meeting
        WHERE room_id = NEW.room_id
        AND time_range && NEW.time_range
        AND id <> NEW.id
    ) THEN
        RAISE EXCEPTION 'Meeting time ranges cannot overlap for the same room';
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER check_no_time_overlap_trigger
    BEFORE INSERT OR UPDATE ON meeting
    FOR EACH ROW
    EXECUTE FUNCTION check_no_time_overlap();

CREATE INDEX idx_no_time_overlap on public.meeting USING gist(room_id,time_range); -- will error out in target
```

---

### PostgreSQL extensions are not supported by target YugabyteDB

**Documentation**: [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)

**Description**: If you have any PostgreSQL extension that is not supported by the target YugabyteDB, they result in the following errors during import schema:

```output
ERROR:  could not open extension control file "/home/centos/yb/postgres/share/extension/<extension_name>.control": No such file or directory
```

**Workaround**: Remove the extension from the exported schema.

**Example**

An example schema on the source database is as follows:

```sql
CREATE EXTENSION IF NOT EXISTS postgis WITH SCHEMA public;
```

---

### Deferrable constraint on constraints other than foreign keys is not supported

**GitHub**: [Issue #1709](https://github.com/yugabyte/yugabyte-db/issues/1709)

**Description**: If you have deferrable constraints on constraints other than foreign keys (for example, UNIQUE constraints) which are currently not supported in the target YugabyteDB, it errors out in the import schema phase as follows:

```output
ERROR: DEFERRABLE unique constraints are not supported yet
```

**Workaround**: Currently, there is no workaround in the target YugabyteDB. Remove it from the exported schema and modify the application for such constraints before pointing it to YugabyteDB.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.users (
    id int PRIMARY KEY,
    email text
);

ALTER TABLE ONLY public.users
    ADD CONSTRAINT users_email_key UNIQUE (email) DEFERRABLE;
```

---

### Data ingestion on XML data type is not supported

**GitHub**: [Issue #1043](https://github.com/yugabyte/yugabyte-db/issues/1043)

**Description**: If you have XML datatype in the source database, it errors out in the import data to target YugabyteDB phase as data ingestion is not allowed on this data type:

```output
 ERROR: unsupported XML feature (SQLSTATE 0A000)
```

**Workaround**: To migrate the data, a workaround is to convert the type to text and import the data to target; to read the data on the target YugabyteDB, you need to create some user defined functions similar to XML functions.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE xml_example (
      id integer,
      data xml
);
```

---

### GiST, BRIN, and SPGIST index types are not supported

**GitHub**: [Issue #1337](https://github.com/yugabyte/yugabyte-db/issues/1337)

**Description**: If you have GiST, BRIN, and SPGIST indexes on the source database, it errors out in the import schema phase with the following error:

```output
 ERROR: index method "gist" not supported yet (SQLSTATE XX000)

```

**Workaround**: Currently, there is no workaround; remove the index from the exported schema.

**Example**

An example schema on the source database is as follows:

```sql
CREATE INDEX gist_idx ON public.ts_query_table USING gist (query);
```

---

### Indexes on some complex data types are not supported

**GitHub**: [Issue #9698](https://github.com/yugabyte/yugabyte-db/issues/9698), [Issue #23829](https://github.com/yugabyte/yugabyte-db/issues/23829), [Issue #17017](https://github.com/yugabyte/yugabyte-db/issues/17017)

**Description**: If you have indexes on some complex types such as TSQUERY, TSVECTOR, JSONB, ARRAYs, INET, UDTs, citext, and so on, those will error out in import schema phase with the following error:

```output
 ERROR:  INDEX on column of type '<TYPE_NAME>' not yet supported
```

**Workaround**: Currently, there is no workaround, but you can cast these data types in the index definition to supported types, which may require adjustments on the application side when querying the column using the index. Ensure you address these changes before modifying the schema.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.citext_type (
    id integer,
    data public.citext
);

CREATE TABLE public.documents (
    id integer NOT NULL,
    title_tsvector tsvector,
    content_tsvector tsvector
);

CREATE TABLE public.ts_query_table (
    id integer,
    query tsquery
);

CREATE TABLE public.test_json (
    id integer,
    data jsonb
);

CREATE INDEX tsvector_idx ON public.documents  (title_tsvector);

CREATE INDEX tsquery_idx ON public.ts_query_table (query);

CREATE INDEX idx_citext ON public.citext_type USING btree (data);

CREATE INDEX idx_json ON public.test_json (data);
```

---

### Constraint trigger is not supported

**GitHub**: [Issue #4700](https://github.com/yugabyte/yugabyte-db/issues/4700)

**Description**: If you have constraint triggers in your source database, as they are currently unsupported in YugabyteDB, and they will error out as follows:

```output
 ERROR:  CREATE CONSTRAINT TRIGGER not supported yet
```

**Workaround**: Currently, there is no workaround; remove the constraint trigger from the exported schema and modify the applications if they are using these triggers before pointing it to YugabyteDB.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.users (
    id    int,
    email character varying(255)
);

CREATE FUNCTION public.check_unique_username() RETURNS trigger
    LANGUAGE plpgsql
AS $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM users
        WHERE email = NEW.email AND id <> NEW.id
    ) THEN
        RAISE EXCEPTION 'Email % already exists.', NEW.email;
    END IF;
    RETURN NEW;
END;
$$;

CREATE CONSTRAINT TRIGGER check_unique_username_trigger
    AFTER INSERT OR UPDATE ON public.users
    DEFERRABLE INITIALLY DEFERRED
    FOR EACH ROW
    EXECUTE FUNCTION public.check_unique_username();
```

---

### Table inheritance is not supported

**GitHub**: [Issue #5956](https://github.com/yugabyte/yugabyte-db/issues/5956)

**Description**: If you have table inheritance in the source database, it will error out in the target as it is not currently supported in YugabyteDB:

```output
ERROR: INHERITS not supported yet
```

**Workaround**: Currently, there is no workaround.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.cities (
    name text,
    population real,
    elevation integer
);

CREATE TABLE public.capitals (
    state character(2) NOT NULL
)
INHERITS (public.cities);
```

---

### %Type syntax is not supported

**GitHub**: [Issue #23619](https://github.com/yugabyte/yugabyte-db/issues/23619)

**Description**: If you have any function, procedure, or trigger using the `%TYPE` syntax for referencing a type of a column from a table, then it errors out in YugabyteDB with the following error:

```output
ERROR: invalid type name "employees.salary%TYPE" (SQLSTATE 42601)
```

**Workaround**: Fix the syntax to include the actual type name instead of referencing the type of a column.

**Example**

An example schema on the source database is as follows:

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

### GIN indexes on multiple columns are not supported

**GitHub**: [Issue #724](https://github.com/yugabyte/yb-voyager/issues/724)

**Description**: If there are GIN indexes in the source schema on multiple columns, they result in an error during import schema as follows:

```output
ERROR: access method "ybgin" does not support multicolumn indexes (SQLSTATE 0A000)
```

**Workaround**: Currently, as there is no workaround, modify the schema to not include such indexes.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.test_gin_json (
    id     integer,
    text   jsonb,
    text1  jsonb
);

CREATE INDEX gin_multi_on_json
    ON public.test_gin_json USING gin (text, text1);
```

---

### Policies on users in source require manual user creation

**GitHub**: [Issue #1655](https://github.com/yugabyte/yb-voyager/issues/1655)

**Description**: If there are policies in the source schema for USERs in the database, the USERs have to be created manually on the target YugabyteDB, as currently the migration of USER/GRANT is not supported. Skipping the manual user creation will return an error during import schema as follows:

```output
ERROR: role "<role_name>" does not exist (SQLSTATE 42704)
```

**Workaround**: Create the USERs manually on target before import schema to create policies.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.z1 (
    a integer,
    b text
);
CREATE ROLE regress_rls_group;
CREATE POLICY p2 ON public.z1 TO regress_rls_group USING (((a % 2) = 1));
```

---

### VIEW WITH CHECK OPTION is not supported

**GitHub**: [Issue #22716](https://github.com/yugabyte/yugabyte-db/issues/22716)

**Description**: If there are VIEWs with check option in the source database, they error out during the import schema phase as follows:

```output
ERROR:  VIEW WITH CHECK OPTION not supported yet
```

**Workaround**: You can use a TRIGGER with INSTEAD OF clause on INSERT/UPDATE on view to achieve this functionality, but it may require application-side adjustments to handle different errors instead of constraint violations.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.employees (
    employee_id    integer NOT NULL,
    employee_name  text,
    salary         numeric
);

CREATE VIEW public.employees_less_than_12000 AS
    SELECT
        employees.employee_id,
        employees.employee_name,
        employees.salary
    FROM
        public.employees
    WHERE
        employees.employee_id < 12000
    WITH CASCADED CHECK OPTION;
```

Suggested change to the schema is as follows:

```sql
SELECT
    employees.employee_id,
    employees.employee_name,
    employees.salary
FROM
    public.employees
WHERE
    employees.employee_id < 12000;

CREATE OR REPLACE FUNCTION modify_employees_less_than_12000()
RETURNS TRIGGER AS $$
BEGIN
    -- Handle INSERT operations
    IF TG_OP = 'INSERT' THEN
        IF NEW.employee_id < 12000 THEN
            INSERT INTO employees(employee_id, employee_name, salary)
            VALUES (NEW.employee_id, NEW.employee_name, NEW.salary);
            RETURN NEW;
        ELSE
            RAISE EXCEPTION 'new row violates check option for view "employees_less_than_12000"; employee_id must be less than 12000';
        END IF;

    -- Handle UPDATE operations
    ELSIF TG_OP = 'UPDATE' THEN
        IF NEW.employee_id < 12000 THEN
            UPDATE employees
            SET employee_name = NEW.employee_name,
                salary = NEW.salary
            WHERE employee_id = OLD.employee_id;
            RETURN NEW;
        ELSE
            RAISE EXCEPTION 'new row violates check option for view "employees_less_than_12000"; employee_id must be less than 12000';
        END IF;
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_modify_employee_12000
    INSTEAD OF INSERT OR UPDATE ON employees_less_than_12000
    FOR EACH ROW
    EXECUTE FUNCTION modify_employees_less_than_12000();
```

---

### UNLOGGED table is not supported

**GitHub**: [Issue #1129](https://github.com/yugabyte/yugabyte-db/issues/1129)

**Description**: If there are UNLOGGED tables in the source schema, they will error out during the import schema with the following error as it is not supported in target YugabyteDB.

```output
ERROR:  UNLOGGED database object not supported yet
```

**Workaround**: Convert it to a LOGGED table.

**Fixed In**: {{<release "2024.2.0.0, 2.25">}}

**Example**

An example schema on the source database is as follows:

```sql
CREATE UNLOGGED TABLE tbl_unlogged (
  id int,
  val text
);
```

Suggested change to the schema is as follows:

```sql
CREATE TABLE tbl_unlogged (
  id int,
  val text
);
```

---

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

### Exporting data with names for tables/functions/procedures using special characters/whitespaces fails

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

### Unsupported datatypes by YugabyteDB

**GitHub**: [Issue 11323](https://github.com/yugabyte/yugabyte-db/issues/11323), [Issue 1731](https://github.com/yugabyte/yb-voyager/issues/1731)

**Description**: The migration skips databases that have the following data types on any column: `GEOMETRY`, `GEOGRAPHY`, `BOX2D`, `BOX3D`, `TOPOGEOMETRY`, `RASTER`, `PG_LSN`, or `TXID_SNAPSHOT`.

**Workaround**: None.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE public.locations (
    id integer NOT NULL,
    name character varying(100),
    geom geometry(Point,4326)
 );

```

---

### Unsupported datatypes by Voyager during live migration

**GitHub**: [Issue 1731](https://github.com/yugabyte/yb-voyager/issues/1731)

**Description**: For live migration, the migration skips data from source databases that have the following data types on any column: `POINT`, `LINE`, `LSEG`, `BOX`, `PATH`, `POLYGON`, or `CIRCLE`.

For live migration with fall-forward/fall-back, the migration skips data from source databases that have the following data types on any column: `HSTORE`, `POINT`, `LINE`, `LSEG`, `BOX`, `PATH`, `POLYGON`, `TSVECTOR`, `TSQUERY`, `CIRCLE`, or `ARRAY OF ENUMS`.

**Workaround**: None.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE combined_tbl (
    id int,
    l line,
    ls lseg,
    p point,
    p1 path,
    p2 polygon
);
```

---

### XID functions is not supported

**GitHub**: [Issue #15638](https://github.com/yugabyte/yugabyte-db/issues/15638)

**Description**: If you have XID datatypes in the source database, its functions, such as, `txid_current()` are not yet supported in YugabyteDB and will result in an error in the target as follows:

```output
 ERROR: Yugabyte does not support xid
```

**Workaround**: None.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE xid_example (
      id integer,
      tx_id xid
);
```

---

### REFERENCING clause for triggers

**GitHub**: [Issue #1668](https://github.com/yugabyte/yugabyte-db/issues/1668)

**Description**: If you have the REFERENCING clause (transition tables) in triggers in source schema, the trigger creation will fail in import schema as it is not currently supported in YugabyteDB.

```output
ERROR:  REFERENCING clause (transition tables) not supported yet
```

**Workaround**: Currently, there is no workaround.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE projects (
    id SERIAL PRIMARY KEY,
    name TEXT,
    region TEXT
);

CREATE OR REPLACE FUNCTION log_deleted_projects()
RETURNS TRIGGER AS $$
BEGIN
    --logic to use the old_table for deleted rows
    SELECT id, name, region FROM old_table;

END;
$$ LANGUAGE plpgsql


CREATE TRIGGER projects_loose_fk_trigger
AFTER DELETE ON projects
REFERENCING OLD TABLE AS old_table
FOR EACH STATEMENT
EXECUTE FUNCTION log_deleted_projects();
```

---

### BEFORE ROW triggers on partitioned tables

**GitHub**: [Issue #24830](https://github.com/yugabyte/yugabyte-db/issues/24830)

**Description**: If you have the BEFORE ROW triggers on partitioned tables in source schema, the trigger creation will fail in import schema as it is not currently supported in YugabyteDB.

```output
ERROR: Partitioned tables cannot have BEFORE / FOR EACH ROW triggers.
```

**Workaround**: Create this trigger on the individual partitions.

**Fixed In**: {{<release "2.25">}}.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE test_partition_trigger (
    id INT,
    val TEXT,
    PRIMARY KEY (id)
) PARTITION BY RANGE (id);

CREATE TABLE test_partition_trigger_part1 PARTITION OF test_partition_trigger
    FOR VALUES FROM (1) TO (100);

CREATE TABLE test_partition_trigger_part2 PARTITION OF test_partition_trigger
    FOR VALUES FROM (100) TO (200);

CREATE OR REPLACE FUNCTION check_and_modify_val()
RETURNS TRIGGER AS $$
BEGIN
    -- Check if id is even; if not, modify `val` to indicate an odd ID
    IF (NEW.id % 2) <> 0 THEN
        NEW.val := 'Odd ID';
    END IF;

    -- Return the row with modifications (if any)
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER before_insert_check
BEFORE INSERT ON test_partition_trigger
FOR EACH ROW
EXECUTE FUNCTION check_and_modify_val();
```

Suggested change to the schema is as follows:

```sql

CREATE TABLE test_partition_trigger (
    id INT,
    val TEXT,
    PRIMARY KEY (id)
) PARTITION BY RANGE (id);

CREATE TABLE test_partition_trigger_part1 PARTITION OF test_partition_trigger
    FOR VALUES FROM (1) TO (100);

CREATE TABLE test_partition_trigger_part2 PARTITION OF test_partition_trigger
    FOR VALUES FROM (100) TO (200);

CREATE OR REPLACE FUNCTION check_and_modify_val()
RETURNS TRIGGER AS $$
BEGIN
    -- Check if id is even; if not, modify `val` to indicate an odd ID
    IF (NEW.id % 2) <> 0 THEN
        NEW.val := 'Odd ID';
    END IF;

    -- Return the row with modifications (if any)
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER before_insert_check
BEFORE INSERT ON test_partition_trigger_part1
FOR EACH ROW
EXECUTE FUNCTION check_and_modify_val();

CREATE TRIGGER before_insert_check
BEFORE INSERT ON test_partition_trigger_part2
FOR EACH ROW
EXECUTE FUNCTION check_and_modify_val();

```

---

### Advisory locks is not yet implemented

**GitHub**: [Issue #3642](https://github.com/yugabyte/yugabyte-db/issues/3642)

**Description**: YugabyteDB does not support PostgreSQL advisory locks (for example, pg_advisory_lock, pg_try_advisory_lock). Any attempt to use advisory locks will result in a "function-not-implemented" error as per the following example:

```sql
yugabyte=# SELECT pg_advisory_lock(100), COUNT(*) FROM cars;
```

```output
ERROR:  advisory locks are not yet implemented
HINT:  If the app doesn't need strict functionality, this error can be silenced by using the GFlag yb_silence_advisory_locks_not_supported_error. See https://github.com/yugabyte/yugabyte-db/issues/3642 for details
```

**Workaround**: Implement a custom locking mechanism in the application to coordinate actions without relying on database-level advisory locks.

---

### System columns is not yet supported

**GitHub**: [Issue #24843](https://github.com/yugabyte/yugabyte-db/issues/24843)

**Description**: System columns, including `xmin`, `xmax`, `cmin`, `cmax`, and `ctid`, are not available in YugabyteDB. Queries or applications referencing these columns will fail as per the following example:

```sql
yugabyte=# SELECT xmin, xmax FROM employees where id = 100;
```

```output
ERROR:  System column "xmin" is not supported yet
```

**Workaround**: Use the application layer to manage tracking instead of relying on system columns.

---

### XML functions is not yet supported

**GitHub**: [Issue #1043](https://github.com/yugabyte/yugabyte-db/issues/1043)

**Description**: XML functions and the XML data type are unsupported in YugabyteDB. If you use functions like `xpath`, `xmlconcat`, and `xmlparse`, it will fail with an error as per the following example:

```sql
yugabyte=# SELECT xml_is_well_formed_content('<project>Alpha</project>') AS is_well_formed_content;
```

```output
ERROR:  unsupported XML feature
DETAIL:  This functionality requires the server to be built with libxml support.
HINT:  You need to rebuild PostgreSQL using --with-libxml.
```

**Workaround**: Convert XML data to JSON format for compatibility with YugabyteDB, or handle XML processing at the application layer before inserting data.

---

### Large Objects and its functions are currently not supported


**GitHub**: Issue [#25318](https://github.com/yugabyte/yugabyte-db/issues/25318)

**Description**: If you have large objects (datatype `lo`) in the source schema and are using large object functions in queries, the migration will fail during import-schema, as large object is not supported in YugabyteDB.

```sql
SELECT lo_create('<OID>');
```

```output
ERROR: Transaction for catalog table write operation 'pg_largeobject_metadata' not found
```

**Workaround**: No workaround is available.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE image (id int, raster lo); 

CREATE TRIGGER t_raster BEFORE UPDATE OR DELETE ON public.image
    FOR EACH ROW EXECUTE FUNCTION lo_manage(raster);
```

### PostgreSQL 12 and later features

**GitHub**: Issue [#25575](https://github.com/yugabyte/yugabyte-db/issues/25575)

**Description**: If any of these PostgreSQL features for version 12 and later are present in the source schema, the import schema step on the target YugabyteDB will fail as YugabyteDB is currently PG11 compatible.

- [JSON Constructor functions](https://www.postgresql.org/about/featurematrix/detail/395/) - `JSON_ARRAY_AGG`, `JSON_ARRAY`, `JSON_OBJECT`, `JSON_OBJECT_AGG`.
- [JSON query functions](https://www.postgresql.org/docs/17/functions-json.html#FUNCTIONS-SQLJSON-TABLE) - `JSON_QUERY`, `JSON_VALUE`, `JSON_EXISTS`, `JSON_TABLE`.
- [IS JSON predicate clause](https://www.postgresql.org/about/featurematrix/detail/396/).
- Any Value [Aggregate function](https://www.postgresql.org/docs/16/functions-aggregate.html#id-1.5.8.27.5.2.4.1.1.1.1) - `any_value`.
- [COPY FROM command with ON_ERROR](https://www.postgresql.org/about/featurematrix/detail/433/) option.
- [Non-decimal integer literals](https://www.postgresql.org/about/featurematrix/detail/407/).
- [Non-deterministic collations](https://www.postgresql.org/docs/12/collation.html#COLLATION-NONDETERMINISTIC).
- [COMPRESSION clause](https://www.postgresql.org/docs/current/sql-createtable.html#SQL-CREATETABLE-PARMS-COMPRESSION) in TABLE Column for TOASTing method.
- [CREATE DATABASE options](https://www.postgresql.org/docs/15/sql-createdatabase.html) (locale, collation, strategy, and oid related).

Apart from these, the following issues are supported in YugabyteDB [v2.25](/preview/releases/ybdb-releases/v2.25), which supports PostgreSQL 15.

- [Multirange datatypes](https://www.postgresql.org/docs/current/rangetypes.html#RANGETYPES-BUILTIN).
- [UNIQUE NULLS NOT DISTINCT clause](https://www.postgresql.org/about/featurematrix/detail/392/) in constraint and index.
- [Range Aggregate functions](https://www.postgresql.org/docs/16/functions-aggregate.html#id-1.5.8.27.5.2.4.1.1.1.1) - `range_agg`, `range_intersect_agg`.
- [FETCH FIRST … WITH TIES in select](https://www.postgresql.org/docs/13/sql-select.html#SQL-LIMIT) statement.
- [Regex functions](https://www.postgresql.org/about/featurematrix/detail/367/) - `regexp_count`, `regexp_instr`, `regexp_like`.
- [Foreign key references](https://www.postgresql.org/about/featurematrix/detail/319/) to partitioned table.
- [Security invoker views](https://www.postgresql.org/about/featurematrix/detail/389/).
- COPY FROM command with WHERE [clause](https://www.postgresql.org/about/featurematrix/detail/330/).
- [Deterministic attribute](https://www.postgresql.org/docs/12/collation.html#COLLATION-NONDETERMINISTIC) in COLLATION objects.
- [SQL Body in Create function](https://www.postgresql.org/docs/15/sql-createfunction.html#:~:text=a%20new%20session.-,sql_body,-The%20body%20of).
- [Common Table Expressions (With queries) with MATERIALIZED clause](https://www.postgresql.org/docs/current/queries-with.html#QUERIES-WITH-CTE-MATERIALIZATION).

### MERGE command

**GitHub**: Issue [#25574](https://github.com/yugabyte/yugabyte-db/issues/25574)

**Description**: If you are using a Merge query to conditionally insert, update, or delete rows on a table on your source database, then this query will fail once you migrate your apps to YugabyteDB as it is a PostgreSQL 15 feature, and not supported yet.

```output
ERROR:  syntax error at or near "MERGE"
```

**Workaround**: Use the PL/pgSQL function to implement similar functionality on the database.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE customer_account (
    customer_id INT PRIMARY KEY,
    balance NUMERIC(10, 2) NOT NULL
);

INSERT INTO customer_account (customer_id, balance)
VALUES
    (1, 100.00),
    (2, 200.00),
    (3, 300.00);

CREATE TABLE recent_transactions (
    transaction_id SERIAL PRIMARY KEY,
    customer_id INT NOT NULL,
    transaction_value NUMERIC(10, 2) NOT NULL
);
INSERT INTO recent_transactions (customer_id, transaction_value)
VALUES
    (1, 50.00),
    (3, -25.00),
    (4, 150.00);

MERGE INTO customer_account ca
USING recent_transactions t
ON t.customer_id = ca.customer_id
WHEN MATCHED THEN
  UPDATE SET balance = balance + transaction_value
WHEN NOT MATCHED THEN
  INSERT (customer_id, balance)
  VALUES (t.customer_id, t.transaction_value);
```

Suggested schema change is to replace the MERGE command with a PL/pgSQL function similar to the following:

```sql
CREATE OR REPLACE FUNCTION merge_customer_account()
RETURNS void AS $$
BEGIN
    -- Insert new rows or update existing rows in customer_account
    INSERT INTO customer_account (customer_id, balance)
    SELECT customer_id, transaction_value
    FROM recent_transactions
    ON CONFLICT (customer_id) 
    DO UPDATE
    SET balance = customer_account.balance + EXCLUDED.balance;
END;
$$ LANGUAGE plpgsql;
```

### JSONB subscripting

**GitHub**: Issue [#25575](https://github.com/yugabyte/yugabyte-db/issues/25575)

**Description**: If you are using the JSONB subscripting in app queries and in the schema (constraints or default expression) on your source database, then the app query will fail once you migrate your apps to YugabyteDB, and import-schema will fail if any DDL has this feature, as it's a PostgreSQL 15 feature.

```output
ERROR: cannot subscript type jsonb because it is not an array
```

**Workaround**: You can use the Arrow ( `-> / ->>` ) operators to access JSONB fields.

**Fixed In**: {{<release "2.25">}}.

**Example**

An example query / DDL on the source database is as follows:

```sql
SELECT ('{"a": {"b": {"c": "some text"}}}'::jsonb)['a']['b']['c'];

CREATE TABLE test_jsonb_chk (
    id int,
    data1 jsonb,
    CHECK (data1['key']<>'{}')
);
```

Suggested change in query to get it working-

```sql
SELECT ((('{"a": {"b": {"c": "some text"}}}'::jsonb)->'a')->'b')->>'c';

CREATE TABLE test_jsonb_chk (
    id int,
    data1 jsonb,
    CHECK (data1->'key'<>'{}')
);
```

### Events Listen / Notify

**GitHub**: Issue [#1872](https://github.com/yugabyte/yugabyte-db/issues/1872)

**Description**: If your application queries or PL/pgSQL objects rely on **LISTEN/NOTIFY events** in the source PostgreSQL database, these functionalities will not work after migrating to YugabyteDB. Currently, LISTEN/NOTIFY events are a no-op in YugabyteDB, and any attempt to use them will trigger a warning instead of performing the expected event-driven operations:

```sql
WARNING:  LISTEN not supported yet and will be ignored
```

**Workaround**: Currently, there is no workaround.

**Example:**

```sql
LISTEN my_table_changes;
INSERT INTO my_table (name) VALUES ('Charlie');
NOTIFY my_table_changes, 'New row added with name: Charlie';
```

### Two-Phase Commit

**GitHub**: Issue [#11084](https://github.com/yugabyte/yugabyte-db/issues/11084)

**Description**: If your application queries or PL/pgSQL objects rely on [Two-Phase Commit protocol](https://www.postgresql.org/docs/11/two-phase.html) that allows multiple distributed systems to work together in a transactional manner in the source PostgreSQL database, these functionalities will not work after migrating to YugabyteDB. Currently, Two-Phase Commit is not implemented in YugabyteDB and will throw the following error when you attempt to execute the commands:

```sql
ERROR:  PREPARE TRANSACTION not supported yet
```

**Workaround**: Currently, there is no workaround.

### DDL operations within the Transaction

**GitHub**:  Issue [#1404](https://github.com/yugabyte/yugabyte-db/issues/1404)

**Description**: If your application queries or PL/pgSQL objects runs DDL operations inside transactions in the source PostgreSQL database, this functionality will not work after migrating to YugabyteDB. Currently, DDL operations in a transaction in YugabyteDB is not supported and will not work as expected.

**Workaround**: Currently, there is no workaround.

**Example:**

```sql
yugabyte=# \d test
Did not find any relation named "test".
yugabyte=# BEGIN;
BEGIN
yugabyte=*# CREATE TABLE test(id int, val text);
CREATE TABLE
yugabyte=*# \d test
                Table "public.test"
 Column |  Type   | Collation | Nullable | Default 
--------+---------+-----------+----------+---------
 id     | integer |           |          | 
 val    | text    |           |          | 
yugabyte=*# ROLLBACK;
ROLLBACK
yugabyte=# \d test
                Table "public.test"
 Column |  Type   | Collation | Nullable | Default 
--------+---------+-----------+----------+---------
 id     | integer |           |          | 
 val    | text    |           |          | 
```
