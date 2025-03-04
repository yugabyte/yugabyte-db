CREATE DATABASE taqo_seek_next_estimation with colocation = true;
\c taqo_seek_next_estimation
SET statement_timeout = '7200s';
-- CREATE QUERIES
create table t1 (k1 int, PRIMARY KEY (k1 asc));
create table t2 (k1 int, k2 int, PRIMARY KEY (k1 asc, k2 asc));
create table t3 (k1 int, k2 int, k3 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc));
create table t4 (k1 int, k2 int, k3 int, k4 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc, k4 asc));
create table t5 (k1 int, k2 int, k3 int, k4 int, k5 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc, k4 asc, k5 asc));
create table tcor_1 (k1 int, k2 int, k3 int, k4 int);
create index tcor_1_idx on tcor_1 (k1 asc, k2 asc, k3 asc, k4 asc);
create statistics stx_tcor_1 on k1, k2, k3, k4 FROM tcor_1;
create table tcor_2 (k1 int, k2 int, k3 int, k4 int);
create index tcor_2_idx on tcor_2 (k1 asc, k2 asc, k3 asc, k4 asc);
create statistics stx_tcor_2 on k1, k2, k3, k4 FROM tcor_2;
CREATE TABLE tcor_3 (k1 INT, k2 INT, k3 INT);
CREATE INDEX tcor_3_idx on tcor_3 (k1 ASC, k2 ASC, k3 ASC);
CREATE STATISTICS stx_tcor_3 on k1, k2, k3 FROM tcor_3;

SET yb_non_ddl_txn_for_sys_tables_allowed = ON;

UPDATE pg_class SET reltuples = 20, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't1' OR relname = 't1_pkey');
UPDATE pg_class SET reltuples = 20, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't1_pkey' OR relname = 't1_pkey_pkey');
UPDATE pg_class SET reltuples = 400, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't2' OR relname = 't2_pkey');
UPDATE pg_class SET reltuples = 400, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't2_pkey' OR relname = 't2_pkey_pkey');
UPDATE pg_class SET reltuples = 8000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't3' OR relname = 't3_pkey');
UPDATE pg_class SET reltuples = 8000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't3_pkey' OR relname = 't3_pkey_pkey');
UPDATE pg_class SET reltuples = 160000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't4' OR relname = 't4_pkey');
UPDATE pg_class SET reltuples = 160000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't4_pkey' OR relname = 't4_pkey_pkey');
UPDATE pg_class SET reltuples = 3200000.0, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't5' OR relname = 't5_pkey');
UPDATE pg_class SET reltuples = 3200000.0, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't5_pkey' OR relname = 't5_pkey_pkey');
UPDATE pg_class SET reltuples = 12500, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_1' OR relname = 'tcor_1_pkey');
UPDATE pg_class SET reltuples = 12500, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_1_idx' OR relname = 'tcor_1_idx_pkey');
UPDATE pg_class SET reltuples = 100000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_2' OR relname = 'tcor_2_pkey');
UPDATE pg_class SET reltuples = 100000, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_2_idx' OR relname = 'tcor_2_idx_pkey');
UPDATE pg_class SET reltuples = 171700, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_3' OR relname = 'tcor_3_pkey');
UPDATE pg_class SET reltuples = 171700, relpages = 0, relallvisible = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 'tcor_3_idx' OR relname = 'tcor_3_idx_pkey');

DELETE FROM pg_statistic WHERE starelid = 'public.t1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t1'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t1'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, -1::real, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, NULL::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}'::real[], '{0.09975062}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}'::real[], '{0.09975623}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05}'::real[], '{0.052493438}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.051733334, 0.051533334, 0.051333334, 0.051266667, 0.050933335, 0.0508, 0.050533332, 0.050533332, 0.050466668, 0.050433334, 0.0502, 0.0502, 0.049933333, 0.0498, 0.049266666, 0.0488, 0.048733335, 0.048333332, 0.048033334, 0.047133334}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{8, 13, 19, 3, 1, 10, 5, 11, 16, 18, 6, 20, 4, 9, 14, 7, 17, 12, 15, 2}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.051933333, 0.0519, 0.0516, 0.051533334, 0.050766665, 0.050766665, 0.050333332, 0.0503, 0.0501, 0.0501, 0.049966667, 0.0499, 0.049733333, 0.049233332, 0.049133334, 0.049133334, 0.0488, 0.048566666, 0.048466668, 0.047733333}'::real[], '{0.10424456}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{16, 14, 6, 15, 2, 17, 4, 19, 1, 5, 20, 13, 12, 18, 3, 9, 7, 8, 10, 11}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0528, 0.051633332, 0.0516, 0.051033333, 0.051, 0.0508, 0.050366666, 0.0503, 0.0501, 0.050066665, 0.049866665, 0.0497, 0.04963333, 0.0496, 0.0491, 0.0489, 0.0488, 0.048733335, 0.048133332, 0.047833335}'::real[], '{0.05457672}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{13, 6, 4, 15, 1, 16, 17, 20, 11, 9, 19, 18, 14, 8, 7, 5, 10, 3, 2, 12}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.052633334, 0.051633332, 0.051066667, 0.050766665, 0.050633334, 0.050633334, 0.050533332, 0.050466668, 0.050466668, 0.0503, 0.050166667, 0.0499, 0.0498, 0.049733333, 0.0496, 0.0493, 0.0489, 0.0488, 0.047733333, 0.046933334}'::real[], '{0.047732644}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 8, 13, 2, 4, 5, 20, 3, 14, 16, 18, 15, 7, 17, 6, 10, 9, 19, 12, 11}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.052233335, 0.0516, 0.051433332, 0.0514, 0.0508, 0.0508, 0.05073333, 0.050633334, 0.050633334, 0.050633334, 0.050333332, 0.050166667, 0.049866665, 0.049833335, 0.049766667, 0.048566666, 0.0485, 0.047733333, 0.047666665, 0.046666667}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{10, 4, 2, 5, 13, 15, 19, 7, 14, 16, 3, 11, 9, 12, 17, 1, 18, 20, 8, 6}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.051966667, 0.051933333, 0.0518, 0.050966665, 0.050933335, 0.0507, 0.0507, 0.050366666, 0.050133333, 0.049866665, 0.0498, 0.049533334, 0.0495, 0.049433332, 0.049266666, 0.049233332, 0.0492, 0.049133334, 0.047966667, 0.047566667}'::real[], '{0.11118402}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{16, 18, 13, 5, 14, 6, 10, 19, 8, 7, 17, 2, 9, 20, 12, 4, 1, 11, 15, 3}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0528, 0.0521, 0.052, 0.0515, 0.050866667, 0.050633334, 0.0503, 0.050233334, 0.050166667, 0.0501, 0.049966667, 0.0495, 0.0494, 0.0493, 0.0492, 0.048933335, 0.048766665, 0.0484, 0.048333332, 0.0475}'::real[], '{0.04169254}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{6, 20, 8, 10, 19, 17, 13, 9, 7, 12, 1, 2, 14, 11, 16, 18, 3, 5, 15, 4}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.052366666, 0.051733334, 0.051666666, 0.051133335, 0.0507, 0.0503, 0.05026667, 0.050133333, 0.050033335, 0.05, 0.05, 0.049666665, 0.049566668, 0.049533334, 0.049466666, 0.0493, 0.048633333, 0.048566666, 0.0485, 0.048433334}'::real[], '{0.05410762}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{20, 2, 19, 7, 11, 14, 13, 17, 5, 1, 16, 15, 8, 18, 12, 3, 10, 6, 4, 9}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k5');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k5'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.051466666, 0.051233333, 0.051166665, 0.050833333, 0.050766665, 0.05073333, 0.05073333, 0.050633334, 0.050566666, 0.050166667, 0.0501, 0.050033335, 0.04963333, 0.049566668, 0.0494, 0.0493, 0.048933335, 0.0489, 0.0489, 0.046933334}'::real[], '{0.040591933}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{15, 6, 9, 2, 19, 4, 13, 16, 1, 12, 14, 3, 11, 10, 20, 5, 8, 7, 17, 18}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.tcor_1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 11::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.09992}'::real[], '{0.08880379}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 0}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.tcor_1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 51::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.01992}'::real[], '{0.008795089}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 0}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.tcor_1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 251::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004}'::real[], NULL::real[], '{-0.0074793934}'::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{0, 101, 103, 104, 106, 107, 109, 110, 112, 113, 115, 116, 118, 119, 121, 122, 124, 125, 127, 128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 161, 163, 164, 166, 167, 169, 170, 172, 173, 175, 176, 178, 179, 181, 182, 184, 185, 187, 188, 190, 191, 193, 194, 196, 197, 199, 200, 202, 203, 205, 206, 208, 209, 211, 212, 214, 215, 217, 218, 220, 221, 223, 224, 226, 227, 229, 230, 232, 233, 235, 236, 238, 239, 241, 242, 244, 245, 247, 248, 250}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.tcor_1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_1'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 50::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02}'::real[], '{0.021729141}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.tcor_2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 18::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.18936667, 0.18833333, 0.15046667, 0.1499, 0.09246667, 0.09243333, 0.045433335, 0.0443, 0.017966667, 0.017866667, 0.0046, 0.004166667, 0.0012, 0.0009666667}'::real[], NULL::real[], '{0.14629024}'::real[], NULL::real[], NULL::real[], array_in('{0, -1, 1, -2, 2, -3, -4, 3, -5, 4, -6, 5, -7, 6}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{-9, -8, 7, 8}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.tcor_2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 17::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.1924, 0.19173333, 0.15066667, 0.14873333, 0.09173334, 0.0902, 0.045633335, 0.044233333, 0.0161, 0.015466667, 0.0054, 0.0051, 0.0012666667, 0.001}'::real[], NULL::real[], '{0.14077911}'::real[], NULL::real[], NULL::real[], array_in('{-1, 0, 1, -2, 2, -3, -4, 3, -5, 4, 5, -6, -7, 6}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{-8, 7, 8}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.tcor_2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 18::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.19436666, 0.1893, 0.14933333, 0.14896667, 0.0927, 0.091133334, 0.045466665, 0.0434, 0.016366666, 0.0161, 0.0050333333, 0.005, 0.0012666667, 0.0010333334}'::real[], NULL::real[], '{0.14582634}'::real[], NULL::real[], NULL::real[], array_in('{0, -1, 1, -2, -3, 2, -4, 3, -5, 4, -6, 5, 6, -7}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{-9, -8, 7, 8}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.tcor_2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_2'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 17::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.1918, 0.18863334, 0.15426667, 0.14693333, 0.0936, 0.0908, 0.044466667, 0.044333335, 0.0168, 0.015133333, 0.0058333334, 0.005133333, 0.001, 0.00093333336}'::real[], NULL::real[], '{0.12975658}'::real[], NULL::real[], NULL::real[], array_in('{-1, 0, -2, 1, 2, -3, 3, -4, -5, 4, 5, -6, 6, -7}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{-9, 7, 7}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.tcor_3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 98::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0305, 0.029366666, 0.028533334, 0.027166666, 0.027033333, 0.026466666, 0.0262, 0.025466667, 0.025066666, 0.025, 0.023066666, 0.022766666, 0.0224, 0.0223, 0.022066666, 0.021333333, 0.0212, 0.020433333, 0.0201, 0.019233333, 0.0187, 0.018566666, 0.0178, 0.0174, 0.016666668, 0.016633334, 0.016366666, 0.015733333, 0.015533334, 0.014733333, 0.014433334, 0.013933334, 0.0139, 0.0134333335, 0.012633333, 0.012233334, 0.011666667, 0.011566667, 0.0112, 0.011033333, 0.010866666, 0.010566667, 0.0105, 0.009433334, 0.0091, 0.008933334, 0.0083, 0.0082, 0.008033333, 0.007866667, 0.0077333334, 0.0075333333, 0.007333333, 0.0063333334, 0.0058, 0.005633333, 0.0055333334, 0.0054, 0.005266667, 0.004766667, 0.0047, 0.0046666665, 0.0045333332, 0.004466667, 0.0038333333, 0.0035666667, 0.0034333332, 0.0034, 0.0029666666, 0.0029333334, 0.0028666668, 0.0022666666, 0.0021333334, 0.0020666667, 0.0019666667, 0.0018333333, 0.0017333333, 0.0017, 0.0015333333, 0.0015, 0.0012, 0.0010666667, 0.001, 0.0008, 0.00076666666, 0.00066666666, 0.00053333334, 0.00053333334}'::real[], NULL::real[], '{0.02486376}'::real[], NULL::real[], NULL::real[], array_in('{99, 100, 96, 97, 98, 93, 95, 94, 92, 91, 88, 89, 90, 87, 86, 84, 83, 85, 82, 81, 80, 79, 78, 77, 73, 76, 75, 70, 72, 74, 71, 69, 68, 67, 65, 66, 63, 61, 59, 62, 64, 58, 60, 56, 57, 55, 52, 50, 54, 49, 53, 51, 48, 47, 45, 46, 42, 43, 44, 41, 38, 40, 37, 39, 36, 33, 34, 32, 35, 30, 31, 27, 29, 26, 24, 25, 23, 28, 22, 21, 20, 18, 19, 16, 17, 14, 11, 15}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{1, 6, 7, 9, 9, 10, 12, 12, 13, 13}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.tcor_3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 100::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0158, 0.015433333, 0.015233333, 0.015233333, 0.015, 0.014966667, 0.014833333, 0.014766667, 0.014733333, 0.014733333, 0.014666666, 0.014666666, 0.014633333, 0.0145333335, 0.014366667, 0.014366667, 0.014266667, 0.014266667, 0.014233333, 0.014166667, 0.014166667, 0.014166667, 0.014066666, 0.0139, 0.013766667, 0.013733333, 0.013666667, 0.013633333, 0.013633333, 0.0136, 0.013333334, 0.0133, 0.0131, 0.013033333, 0.013033333, 0.012933333, 0.012933333, 0.012866667, 0.012666667, 0.012666667, 0.012633333, 0.0125, 0.012433333, 0.012433333, 0.0122, 0.011733334, 0.011666667, 0.0115, 0.0115, 0.0113, 0.011266666, 0.011166667, 0.0107, 0.0105, 0.010433333, 0.0103, 0.0101666665, 0.0101, 0.009766666, 0.009433334, 0.0092, 0.0088, 0.008766667, 0.008633333, 0.008433334, 0.008433334, 0.0084, 0.008333334, 0.008233333, 0.008233333, 0.0081, 0.008033333, 0.0078, 0.0072666667, 0.0064, 0.0063333334, 0.006233333, 0.0061666667, 0.0061666667, 0.006, 0.0059666666, 0.0054666665, 0.004766667, 0.0046, 0.0043, 0.0041333335, 0.0035333333, 0.0035, 0.0034666667, 0.0032, 0.003, 0.0024666667, 0.0022666666, 0.0018, 0.0016333334, 0.0016, 0.0014, 0.0010333334, 0.00056666665, 0.0005}'::real[], '{0.024258668}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{50, 59, 47, 54, 62, 49, 44, 57, 42, 48, 38, 43, 55, 52, 45, 46, 56, 64, 68, 51, 53, 61, 41, 39, 37, 36, 66, 35, 65, 40, 63, 58, 67, 29, 33, 32, 60, 34, 30, 69, 71, 72, 31, 73, 75, 25, 74, 26, 70, 28, 22, 27, 76, 79, 78, 24, 23, 77, 21, 82, 20, 83, 81, 84, 18, 85, 80, 16, 15, 17, 86, 14, 19, 87, 90, 88, 10, 11, 13, 12, 89, 91, 92, 9, 93, 8, 95, 6, 94, 5, 7, 96, 4, 97, 3, 98, 99, 2, 100, 1}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL, NULL); 
DELETE FROM pg_statistic WHERE starelid = 'public.tcor_3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.tcor_3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.tcor_3'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 98::real, 1::smallint, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.029266667, 0.028866667, 0.0276, 0.027566666, 0.026766667, 0.026633333, 0.026366666, 0.026, 0.025866667, 0.025733333, 0.024833333, 0.0233, 0.022333333, 0.021733332, 0.021066668, 0.021066668, 0.020766666, 0.020633332, 0.019433333, 0.019033333, 0.019033333, 0.018833334, 0.018266667, 0.018133333, 0.017533334, 0.0164, 0.016333334, 0.015866667, 0.0145, 0.0145, 0.0143, 0.0143, 0.014, 0.012933333, 0.0129, 0.012666667, 0.011766667, 0.0114, 0.0112, 0.0109, 0.0105, 0.009966667, 0.009666666, 0.009466667, 0.0093, 0.0090333335, 0.008333334, 0.0082, 0.008133333, 0.008, 0.0073, 0.0068666665, 0.0067666667, 0.006733333, 0.006366667, 0.0063, 0.0062, 0.0056666667, 0.0056, 0.0049, 0.004833333, 0.0048, 0.0043666665, 0.004, 0.0037, 0.0036333334, 0.0034333332, 0.0032333334, 0.0030666667, 0.0028333333, 0.0025333334, 0.0025, 0.0022, 0.0021666666, 0.002, 0.0018666667, 0.0016666667, 0.0016, 0.0015666666, 0.0015333333, 0.0011333333, 0.0011, 0.0011, 0.00083333335, 0.00076666666, 0.0006, 0.0006, 0.00046666668, 0.0004, 0.00036666667, 0.00036666667}'::real[], NULL::real[], '{0.030713478}'::real[], NULL::real[], NULL::real[], array_in('{1, 4, 2, 3, 5, 6, 10, 9, 8, 7, 13, 11, 12, 15, 17, 18, 14, 16, 19, 20, 24, 22, 21, 26, 23, 25, 27, 28, 29, 34, 30, 32, 31, 37, 33, 35, 36, 39, 38, 41, 43, 40, 45, 44, 42, 46, 47, 48, 50, 49, 51, 53, 52, 55, 56, 54, 57, 59, 58, 63, 60, 62, 61, 64, 66, 68, 65, 70, 67, 69, 71, 74, 75, 72, 73, 77, 76, 80, 79, 78, 83, 82, 84, 81, 85, 86, 87, 88, 89, 90, 92}', 'pg_catalog.int4'::regtype, -1)::anyarray, array_in('{91, 91, 93, 94, 95, 96, 98}', 'pg_catalog.int4'::regtype, -1)::anyarray, NULL, NULL); 
update pg_yb_catalog_version set current_version=current_version+1 where db_oid=1;
SET yb_non_ddl_txn_for_sys_tables_allowed = OFF;


SET pg_hint_plan.enable_hint = ON;
SET pg_hint_plan.debug_print = ON;
SET client_min_messages TO log;
SET pg_hint_plan.message_level = debug;
SET temp_file_limit="8182MB";
set yb_bnl_batch_size = 1024; set yb_enable_base_scans_cost_model = true;
SET yb_enable_optimizer_statistics = true;
-- Query Hash: fc9a1677584c556c9ae8b2a510738d25
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16);
-- Query Hash: 9274124195113877a48cd356a9b7dfe1
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12);
-- Query Hash: 2e3933a314ffab2ec57eab1f65bc99a7
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12);
-- Query Hash: e7f63532c5f21e987dcf70c5b62f5cb8
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16);
-- Query Hash: 035ed29bec9cf48d0cfccab9ea75393a
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 = 4 and k2 IN (4, 8, 12, 16);
-- Query Hash: d5c50c8a7ac34a8901e60dbb379e3f4b
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 IN (4, 8, 12, 16) and k2 = 4;
-- Query Hash: 8e96c9a0b2caf93157b338bbd13ee9fa
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k3 IN (4, 8, 12, 16) and k4 = 4;
-- Query Hash: 98672ebc721b9dabcd751ae8d1d70bfe
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16);
-- Query Hash: 4e3d6cdf2d3dfd8c9661d9db1e06d06f
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16);
-- Query Hash: b3c453f75f2bfa75e8934afb1d0a000e
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16);
-- Query Hash: c7fd96b45a1a90a438c5c8b51b88bf2f
EXPLAIN (COSTS OFF) SELECT * FROM t3 WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16);
-- Query Hash: 7c3d5db060949e54e6135d9a0d8ea919
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12);
-- Query Hash: 42a855f52bd3a79f8fb3814af23cc20d
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12);
-- Query Hash: 0e9d33c69acb0551957d04fc0fcc5aa1
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k2 IN (4,8,12,16) and k3 <= 4 and k3 > 7 and k4 IN (4, 8, 12);
-- Query Hash: 8b07844ffb72e7e079ec3f59c0986f88
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k3 >= 4 and k3 < 14;
-- Query Hash: 7431fd75b284e9732a0fe4b7d6efc8a2
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14;
-- Query Hash: 0bbbb91ea7ed546300a103f7ebf911b0
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k2 >= 4 and k2 < 14 and k4 >= 4 and k4 < 14;
-- Query Hash: a2d5901879e1db5f406b2dff1ce91e8e
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: 61f218667861386932c71486de344a8f
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: 4fc63a754558f81b3ea788b8ab66b2dd
EXPLAIN (COSTS OFF) SELECT * FROM t3 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: 24482e5009140b10ee7738b1f978e9f6
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: f02c4a2ecd531f683479251b7d3439ce
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: a2d5901879e1db5f406b2dff1ce91e8e
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 >= 4 and k1 < 14;
-- Query Hash: 4772a64709ca191fa2b2096f159e5f3f
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k2 >= 4 and k2 < 14;
-- Query Hash: 24dc55c74b331ad4fd956ac9b6400d8d
EXPLAIN (COSTS OFF) SELECT * FROM t3 WHERE k3 >= 4 and k3 < 14;
-- Query Hash: f425b12c4a6d8d734c69e50afeff04ed
EXPLAIN (COSTS OFF) SELECT * FROM t4 WHERE k4 >= 4 and k4 < 14;
-- Query Hash: 7d901f98ab781317763d9915875de118 , !BEST_PLAN_FOUND
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k5 >= 4 and k5 < 14;
-- Query Hash: b5562a3ef44a737b2846f0975cf9e1c7
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8);
-- Query Hash: 6a8ead9b8b122eea0e5460236bc6c19b , !BEST_PLAN_FOUND
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8, 12);
-- Query Hash: d7eedc71bffb36fa0152da8c08bf7808
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8, 12, 16);
-- Query Hash: 3a022df927f53a2864f9d95af72fee58
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 IN (4, 8, 12, 16);
-- Query Hash: 8c7bf857d9e088af7a4b5c904c5b8610
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k2 IN (4, 8, 12, 16);
-- Query Hash: 5d91fd582d63d5cb7df1e3face8067d5
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k1 < 5;
-- Query Hash: 979b459461ba7b840826695627007334
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k2 < 25;
-- Query Hash: 9a8420c43a0a8e1661e5d1ac9bdd62b0
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k3 < 125;
-- Query Hash: 3324c9ff1941acab32f5415abd90c0c4
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k4 < 25;
-- Query Hash: adbcbd6fb6decb44cd3d786bf910e5d4
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k1 < 5 AND k2 < 25;
-- Query Hash: 25bd3b427c6c6786ffd4bb1ac43d50c9
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k1 < 5 AND k3 < 125;
-- Query Hash: c51689b307246763709d95761488c01e
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k1 < 5 AND k4 < 25;
-- Query Hash: f2621d436cb67e9e38c733ba36c6e3bd
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k2 < 25 AND k3 < 125;
-- Query Hash: c60cbde271e15ebbb324c337b8219e11
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k2 < 25 AND k4 < 25;
-- Query Hash: 9be0c8a2b6797d336f5d43b76e18be46
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k3 < 125 AND k4 < 25;
-- Query Hash: 4fe07880e09bf7812010c348aae8a57d
EXPLAIN (COSTS OFF) SELECT * FROM tcor_1 WHERE k1 < 5 AND k2 < 25 AND k3 < 125 AND k4 < 25;
-- Query Hash: aaa7d9c764b2723a7495d33f609ab200
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k1 < 0;
-- Query Hash: 540441b5cf77f76d10f510ba04556e3d
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k2 < 0;
-- Query Hash: 3b525b570874f819873f09cd386b2c10
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k3 < 0;
-- Query Hash: 90c6e6321ee74099b07569d8aa7e9f05
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k4 < 0;
-- Query Hash: 4d05ca97a12862a4a513929f249524ea
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k1 < 0 AND k2 < 0;
-- Query Hash: 89c00f63e767513c6aa3d08cb9b55f0b
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k1 < 0 AND k3 < 0;
-- Query Hash: cc71e17fa039883b7e8fd219533c8285
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k1 < 0 AND k4 < 0;
-- Query Hash: 60229f8796efe228e27d959b0ea59781
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k2 < 0 AND k3 < 0;
-- Query Hash: 1a883df555044f269f4dc699156a8594
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k2 < 0 AND k4 < 0;
-- Query Hash: 4566c9cfd7224af62bb14a7dd6cf429f
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k3 < 0 AND k4 < 0;
-- Query Hash: 18f5729f5f6a432245c96e7c6ce13cbe
EXPLAIN (COSTS OFF) SELECT * FROM tcor_2 WHERE k1 < 0 AND k2 < 0 AND k3 < 0 AND k4 < 0;
-- Query Hash: 51d11a69af237d21a0f439680ae11e86
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 > 50 and k3 > 50;
-- Query Hash: b0418f7b0f73ba0a1e09526703b0ec60
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 > 50 and k3 > 50;
-- Query Hash: 162202d912755f260a7ba158aaa656e7
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 > 50 and k2 > 50;
-- Query Hash: 92961ecf4a4a096b80054048f778d525
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 = 50 AND k3 > 50;
-- Query Hash: 375c8ae3f86b5dbba917c7108e42b918
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 = 50 AND k3 > 50 AND k3 < 80;
-- Query Hash: d729eb647c6857e30897b4b4a6fec1be
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 = 50 AND k3 > 50;
-- Query Hash: b57c1e01bf6b6658b86167fc6df13845
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 = 50 AND k3 > 50 AND k3 < 80;
-- Query Hash: 024fb5b577416303e3777892ea2f02c9
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k3 = 50 AND k2 > 50;
-- Query Hash: d271787b745e12b73f1d71efdf25856c
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k3 = 50 AND k2 > 50 AND k2 < 80;
-- Query Hash: 16a4b2ae22250f600cc8ddd60a443049
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 IN (50, 60, 70, 80) AND k3 > 50;
-- Query Hash: 32a970e0cc5bed5898970ed39860a744
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 IN (50, 60, 70, 80) AND k3 > 50 AND k3 < 80;
-- Query Hash: 4ae624260329d91797989cfec1fe32b3
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 IN (50, 60, 70, 80) AND k2 IN (50, 60, 70, 80);
-- Query Hash: 17c4b34da306f3aab063529002052144
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k1 IN (50, 60, 70, 80) AND k3 IN (50, 60, 70, 80);
-- Query Hash: b457f2828cc6c6beded8e0b71b63a624
EXPLAIN (COSTS OFF) SELECT * FROM tcor_3 WHERE k2 IN (50, 60, 70, 80) AND k3 IN (50, 60, 70, 80);
-- DROP QUERIES;
DROP TABLE IF EXISTS t1 CASCADE;
DROP TABLE IF EXISTS t2 CASCADE;
DROP TABLE IF EXISTS t3 CASCADE;
DROP TABLE IF EXISTS t4 CASCADE;
DROP TABLE IF EXISTS t5 CASCADE;
DROP TABLE IF EXISTS tcor_1 CASCADE;
DROP TABLE IF EXISTS tcor_2 CASCADE;
DROP TABLE IF EXISTS tcor_3 CASCADE;
