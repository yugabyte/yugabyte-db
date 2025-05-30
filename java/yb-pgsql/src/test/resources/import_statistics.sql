SET yb_non_ddl_txn_for_sys_tables_allowed = ON;

UPDATE pg_class SET reltuples = 6, relpages = 0, relallvisible = 0 WHERE relnamespace = 'test_schema'::regnamespace AND (relname = 'table1' OR relname = 'table1_pkey');
UPDATE pg_class SET reltuples = 5, relpages = 0, relallvisible = 0 WHERE relnamespace = 'test_schema'::regnamespace AND (relname = 'table2' OR relname = 'table2_pkey');

DELETE FROM pg_statistic WHERE starelid = 'test_schema.table1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'test_schema.table1'::regclass and a.attname = 'id');
INSERT INTO pg_statistic VALUES ('test_schema.table1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'test_schema.table1'::regclass and a.attname = 'id'), False::boolean, 0.16666667::real, 4::integer, -0.6666667::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.33333334}'::real[], NULL::real[], '{0.9}'::real[], NULL::real[], NULL::real[], array_in('{2}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{1, 3, 4}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'test_schema.table2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'test_schema.table2'::regclass and a.attname = 'name');
INSERT INTO pg_statistic VALUES ('test_schema.table2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'test_schema.table2'::regclass and a.attname = 'name'), False::boolean, 0.2::real, 6::integer, -0.6::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 98::oid, 664::oid, 664::oid, 0::oid, 0::oid, 100::oid, 100::oid, 100::oid, 0::oid, 0::oid, '{0.4}'::real[], NULL::real[], '{0.4}'::real[], NULL::real[], NULL::real[], array_in('{"Alice"}', 'pg_catalog.text'::regtype, -1)::anyarray, array_in('{"Bob", "Charlie"}', 'pg_catalog.text'::regtype, -1)::anyarray, NULL, NULL);
update pg_yb_catalog_version set current_version=current_version+1 where db_oid=1;
SET yb_non_ddl_txn_for_sys_tables_allowed = OFF;
