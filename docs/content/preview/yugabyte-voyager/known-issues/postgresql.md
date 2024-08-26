---
title: PostgreSQL source database
linkTitle: PostgreSQL
headcontent: What to watch out for when migrating data from PostgreSQL
description: Review limitations and suggested workarounds for migrating data from PostgreSQL.
menu:
  preview_yugabyte-voyager:
    identifier: postgresql-issues
    parent: known-issues
    weight: 102
type: docs
rightNav:
  hideH3: true
---

Review limitations and implement suggested workarounds to successfully migrate data from PostgreSQL to YugabyteDB.

## Contents

- [Adding primary key to a partitioned table results in an error](#adding-primary-key-to-a-partitioned-table-results-in-an-error)
- [Index creation on partitions fail for some YugabyteDB builds](#index-creation-on-partitions-fail-for-some-yugabytedb-builds)
- [Creation of certain views in the rule.sql file](#creation-of-certain-views-in-the-rule-sql-file)
- [Indexes on INET type are not supported](#indexes-on-inet-type-are-not-supported)
- [Create or alter conversion is not supported](#create-or-alter-conversion-is-not-supported)
- [GENERATED ALWAYS AS STORED type column is not supported](#generated-always-as-stored-type-column-is-not-supported)
- [Unsupported ALTER TABLE DDLs variants in source schema](#unsupported-alter-table-ddls-variants-in-schema-in-source-schema)
- [Storage parameters on indexes or constraints in the source PostgreSQL](#storage-parameters-on-indexes-or-constraints-in-the-source-postgresql)
- [Foreign table in the source database requires SERVER and USER MAPPING](#foreign-table-in-the-source-database-requires-server-and-user-mapping)
- [Exclusion constraints is not supported](#exclusion-constraints-is-not-supported)
- [PostgreSQL extensions are not supported by target YugabyteDB](#postgresql-extensions-are-not-supported-by-target-yugabytedb)
- [Deferrable constraint on contraints other than foreign keys is not supported](#deferrable-constraint-on-contraints-other-than-foreign-keys-is-not-supported)
- [Data ingestion on XML data type is not supported](#data-ingestion-on-xml-data-type-is-not-supported)
- [GiST index type is not supported](#gist-index-type-is-not-supported)

### Adding primary key to a partitioned table results in an error

**GitHub**: [Issue #612](https://github.com/yugabyte/yb-voyager/issues/612)

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

---

### Indexes on INET type are not supported

**GitHub**: [Issue #17017](https://github.com/yugabyte/yb-voyager/issues/17017)

**Description**: If there is an index on a column of the INET type, it errors out during import.

**Workaround**: Modify the column to a TEXT type.

**Example**

An example schema on the source database is as follows:

```sql
create table test( id int primary key, f1 inet);
create index test_index on test(f1);
```

The import schema error is as follows:

```sql
INDEXES_table.sql: CREATE INDEX test_index ON public.test USING btree (f1);
ERROR: INDEX on column of type 'INET' not yet supported (SQLSTATE 0A000)
```

Suggested workaround is to change the INET column to TEXT for the index creation to succeed as follows:

```sql
create table test( id int primary key, f1 text);
```

---

### Create or alter conversion is not supported

**GitHub**: [Issue #10866](https://github.com/yugabyte/yugabyte-db/issues/10866)

**Description**: If you have conversions in your PostgreSQL database, they will error out as follows as conversions are not supported in the target YugabyteDB yet:

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

**Description**: If you have tables in the source database with columns of GENERATED ALWAYS AS STORED type (which means the data of this column is derived from some other columns of the table), it will throw syntax error in YugabyteDB as follows:

```output
ERROR: syntax error at or near "(" (SQLSTATE 42601)
```

**Workaround**: Create a trigger on this table that updates its value on any INSERT/UPDATE operation, and set a default value for this column. This provides functionality similar to PostgreSQL's GENERATED ALWAYS AS STORED columns using a trigger.

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

### Unsupported ALTER TABLE DDLs variants in source schema

**GitHub**: [Issue #1124](https://github.com/yugabyte/yugabyte-db/issues/1124)

**Description**: If you have done the following alterations on the source schema, they will come up in the exported schema and are not supported by target YugabyteDB:

1. `ALTER TABLE ONLY table_name ALTER COLUMN column_name SET (prop = value …)`
1. `ALTER TABLE table_name DISABLE RULE rule_name;`
1. `ALTER TABLE table_name CLUSTER ON index_name;`

```output
ERROR: ALTER TABLE ALTER column not supported yet (SQLSTATE 0A000)
ERROR: ALTER TABLE DISABLE RULE not supported yet (SQLSTATE 0A000)
ERROR: ALTER TABLE CLUSTER not supported yet (SQLSTATE 0A000)
```

**Workaround**: For (1) and (3), you have to remove the alterations from the exported schema. For (2), remove the ALTER and the respective RULE as well so that it is not enabled on the table.

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

Suggested change to schema as follows:

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

**Description**: If you have foreign tables in the schema, during the export schema phase, exported schema does not include the SERVER and USER MAPPING objects. So, you must manually create these objects before importing schema, else FOREIGN TABLE creation fails with the following error:

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

**Description**: If you have exclusion constraints on the tables in the source database, those will error out during import schema to target with the following error:

```output
ERROR: EXCLUDE constraint not supported yet (SQLSTATE 0A000)
```

**Workaround**: To implement exclude constraints, follow this workaround:

1. Create a trigger: Set up a TRIGGER for INSERT or UPDATE operations on the table. This trigger will use the specified expression to search the relevant columns for any potential violations.

1. Add indexes: It is crucial to create an INDEX on the columns involved in the expression. This helps ensure that the search operation performed by the trigger does not negatively impact performance.

Note that creating an index on the relevant columns is essential for maintaining performance. Without an index, the trigger's search operation can degrade performance.

**Caveats** : Note that there are specific issues related to [creating indexes on certain data types](LINK to other issue) in YugabyteDB. Depending on the data types involved, additional workarounds may be required to ensure optimal performance for these constraints.

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

Suggested change to schema as follows:

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
```

<!-- The following GiST index is also required for better performance of constraints, because the source constraint uses gist method but the target YugabyteDB doesn't support GIST indexes. need to do some workaround for that as well [refer](LINK DOCS).. -->

---

### PostgreSQL extensions are not supported by target YugabyteDB

**Documentation**: [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)

**Description**: If you have any PostgreSQL extension that is not supported by the target YugabyteDB, then it errors out during import schema as follows:

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

### Deferrable constraint on contraints other than foreign keys is not supported

**GitHub**: [Issue #1709](https://github.com/yugabyte/yugabyte-db/issues/1709)

**Description**: If you have deferrable constraints on constraints other than foreign keys, for example, UNIQUE constraints which are not supported in target YugabyteDB yet, it errors out in import schema phase as follows:

```output
ERROR: DEFERRABLE unique constraints are not supported yet
```

**Workaround**: Currrently, there is no workaround in target YugabyteDB, so remove it from the exported schema and modify the application for such constraints before pointing it on YugabyteDB.

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

**Description**: If you have XML datatype in source database, it errors out in import data to target YugabyteDB phase as data ingestion is not allowed on this data type:

```output
 ERROR: unsupported XML feature (SQLSTATE 0A000)
```

**Workaround**: To migrate the data, a workaround is to convert the type as text data type and get the data imported to target, but to read the data on target YugabyteDB you need to create some user defined functions similar to XML functions.

**Example**

An example schema on the source database is as follows:

```sql
CREATE TABLE xml_example (
      id integer,
      data xml
);
```

---

### GiST index type is not supported

**GitHub**: [Issue #1337](https://github.com/yugabyte/yugabyte-db/issues/1337)

**Description**: If you have GiST indexes on the source database, it errors out in import schema phase in target with the following error:

```output
 ERROR: index method "gist" not supported yet (SQLSTATE XX000)

```

**Workaround**: Currently, there is no workaround, so remove it from the exported schema.

**Example**

An example schema on the source database is as follows:

```sql
CREATE INDEX gist_idx ON public.ts_query_table USING gist (query);
```

---
