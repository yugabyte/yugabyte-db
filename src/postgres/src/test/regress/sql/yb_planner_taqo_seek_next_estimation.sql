CREATE DATABASE taqo_seek_next_estimation with colocation = true;
\c taqo_seek_next_estimation
SET statement_timeout = '7200s';
SET enable_bitmapscan = false; -- TODO(#20573): update bitmap scan cost model
-- CREATE QUERIES
create table t1 (k1 int, PRIMARY KEY (k1 asc));
create table t2 (k1 int, k2 int, PRIMARY KEY (k1 asc, k2 asc));
create table t3 (k1 int, k2 int, k3 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc));
create table t4 (k1 int, k2 int, k3 int, k4 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc, k4 asc));
create table t5 (k1 int, k2 int, k3 int, k4 int, k5 int, PRIMARY KEY (k1 asc, k2 asc, k3 asc, k4 asc, k5 asc));

SET yb_non_ddl_txn_for_sys_tables_allowed = ON;
UPDATE pg_class SET reltuples = 20, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't1' OR relname = 't1_pkey');
UPDATE pg_class SET reltuples = 20, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't1_pkey' OR relname = 't1_pkey_pkey');
UPDATE pg_class SET reltuples = 400, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't2' OR relname = 't2_pkey');
UPDATE pg_class SET reltuples = 400, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't2_pkey' OR relname = 't2_pkey_pkey');
UPDATE pg_class SET reltuples = 8000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't3' OR relname = 't3_pkey');
UPDATE pg_class SET reltuples = 8000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't3_pkey' OR relname = 't3_pkey_pkey');
UPDATE pg_class SET reltuples = 160000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't4' OR relname = 't4_pkey');
UPDATE pg_class SET reltuples = 160000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't4_pkey' OR relname = 't4_pkey_pkey');
UPDATE pg_class SET reltuples = 3200000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't5' OR relname = 't5_pkey');
UPDATE pg_class SET reltuples = 3200000, relpages = 0 WHERE relnamespace = 'public'::regnamespace AND (relname = 't5_pkey' OR relname = 't5_pkey_pkey');
DELETE FROM pg_statistic WHERE starelid = 'public.t1'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t1'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t1'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t1'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, -1::real, 2::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 97::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, NULL::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t2'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t2'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t2'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007}'::real[], '{0.0997506231}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007}'::real[], '{1}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007}'::real[], '{0.0997562334}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t3'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t3'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t3'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007, 0.0500000007}'::real[], '{0.0524934381}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0522999987, 0.0517666675, 0.0517333336, 0.0517333336, 0.0507999994, 0.0504666679, 0.0503666662, 0.0503333323, 0.0502333343, 0.0501333326, 0.0501333326, 0.0499333329, 0.0496666655, 0.0495666675, 0.0494999997, 0.0493666679, 0.0482000001, 0.0481333323, 0.0479666665, 0.0476666652}'::real[], '{0.074490793}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{6, 5, 2, 12, 11, 17, 4, 10, 16, 8, 18, 7, 9, 13, 19, 3, 20, 14, 15, 1}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0514666662, 0.0512333326, 0.0510333329, 0.0508666672, 0.0508333333, 0.0507999994, 0.0507666655, 0.0505666658, 0.0505000018, 0.0500999987, 0.0500333346, 0.0500000007, 0.0500000007, 0.0498000011, 0.0494666658, 0.0492333323, 0.0491666682, 0.0485000014, 0.0482666679, 0.0473666675}'::real[], '{0.0732290298}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{5, 18, 8, 1, 3, 7, 11, 13, 9, 2, 20, 4, 10, 14, 16, 12, 17, 19, 6, 15}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0518999994, 0.0513666682, 0.0511666648, 0.0509666651, 0.0507333316, 0.0505999997, 0.0505000018, 0.0500666648, 0.0500000007, 0.0499666668, 0.0498666652, 0.0498666652, 0.049833335, 0.0498000011, 0.0496999994, 0.0496333316, 0.0495333336, 0.0491666682, 0.0480000004, 0.0473333336}'::real[], '{0.0539749861}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{5, 4, 16, 3, 20, 14, 11, 12, 7, 8, 15, 19, 2, 9, 13, 17, 10, 18, 1, 6}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t4'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.t4'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t4'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0513999984, 0.0511999987, 0.0510666668, 0.050999999, 0.0505000018, 0.0503333323, 0.0502999984, 0.0502666682, 0.0502000004, 0.0499666668, 0.0498999991, 0.0496999994, 0.0496000014, 0.0496000014, 0.0494999997, 0.0493666679, 0.0493000001, 0.0491000004, 0.0489333346, 0.0487666652}'::real[], '{0.0537266955}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{9, 17, 3, 8, 20, 15, 1, 18, 10, 2, 4, 19, 5, 16, 12, 11, 13, 14, 6, 7}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k1');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k1'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0527000017, 0.0525666662, 0.0516333319, 0.0515000001, 0.0513999984, 0.0513999984, 0.0513000004, 0.050433334, 0.0504000001, 0.0502333343, 0.0501666665, 0.0496999994, 0.0495333336, 0.0492666662, 0.0491999984, 0.0489999987, 0.0487000011, 0.0472999997, 0.0468333326, 0.0467333347}'::real[], '{0.0426800177}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{18, 4, 3, 2, 1, 17, 9, 8, 12, 7, 10, 6, 19, 15, 11, 16, 5, 20, 13, 14}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k2');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k2'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0526666678, 0.0524000004, 0.0513666682, 0.0510333329, 0.0509666651, 0.050433334, 0.0504000001, 0.0502666682, 0.0502666682, 0.0502333343, 0.0499333329, 0.0496666655, 0.0494333319, 0.049333334, 0.049333334, 0.0492666662, 0.0486666672, 0.0485333316, 0.0481333323, 0.0476666652}'::real[], '{0.0537041165}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{17, 7, 15, 20, 16, 6, 2, 4, 11, 3, 10, 5, 14, 8, 18, 12, 9, 13, 19, 1}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k3');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k3'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0528333336, 0.0521666668, 0.0510666668, 0.050933335, 0.0508666672, 0.0508333333, 0.0507333316, 0.0507000014, 0.0506666675, 0.0499333329, 0.0499333329, 0.0498000011, 0.0496666655, 0.0494666658, 0.0489000008, 0.0489000008, 0.0487999991, 0.0483999997, 0.0478666648, 0.0475333333}'::real[], '{0.0434316583}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{7, 17, 18, 10, 9, 1, 5, 6, 13, 3, 4, 16, 19, 11, 15, 20, 8, 14, 2, 12}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k4');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k4'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0527000017, 0.0526000001, 0.0513000004, 0.0512333326, 0.0510666668, 0.050999999, 0.0509000011, 0.0508333333, 0.0507000014, 0.0506666675, 0.0501666665, 0.0499666668, 0.0496000014, 0.0494666658, 0.0493000001, 0.0489666648, 0.0483666658, 0.0481333323, 0.0478000008, 0.0452333316}'::real[], '{0.0467358306}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{18, 10, 15, 14, 2, 3, 4, 12, 7, 16, 19, 5, 20, 17, 13, 1, 9, 11, 8, 6}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
DELETE FROM pg_statistic WHERE starelid = 'public.t5'::regclass AND staattnum = (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k5');
INSERT INTO pg_statistic VALUES ('public.t5'::regclass, (SELECT a.attnum FROM pg_attribute a WHERE a.attrelid = 'public.t5'::regclass and a.attname = 'k5'), False::boolean, 0::real, 4::integer, 20::real, 1::smallint, 3::smallint, 0::smallint, 0::smallint, 0::smallint, 96::oid, 97::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, 0::oid, '{0.0513000004, 0.0509000011, 0.0509000011, 0.0507333316, 0.0504000001, 0.0502666682, 0.0502666682, 0.0500666648, 0.0500333346, 0.0500333346, 0.0500333346, 0.0499666668, 0.0499666668, 0.0496333316, 0.0495666675, 0.0494333319, 0.0494333319, 0.0493000001, 0.0489666648, 0.0487999991}'::real[], '{0.06026062}'::real[], NULL::real[], NULL::real[], NULL::real[], array_in('{14, 5, 9, 18, 6, 1, 20, 15, 11, 16, 19, 3, 12, 13, 4, 8, 17, 10, 7, 2}', 'int4'::regtype, -1)::anyarray, NULL, NULL, NULL);
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
-- Query Hash: 7d901f98ab781317763d9915875de118
EXPLAIN (COSTS OFF) SELECT * FROM t5 WHERE k5 >= 4 and k5 < 14;
-- Query Hash: b5562a3ef44a737b2846f0975cf9e1c7
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8);
-- Query Hash: 6a8ead9b8b122eea0e5460236bc6c19b
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8, 12);
-- Query Hash: d7eedc71bffb36fa0152da8c08bf7808
EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 IN (4, 8, 12, 16);
-- Query Hash: 3a022df927f53a2864f9d95af72fee58
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k1 IN (4, 8, 12, 16);
-- Query Hash: 8c7bf857d9e088af7a4b5c904c5b8610
EXPLAIN (COSTS OFF) SELECT * FROM t2 WHERE k2 IN (4, 8, 12, 16);
-- DROP QUERIES;
DROP TABLE IF EXISTS t1 CASCADE;
DROP TABLE IF EXISTS t2 CASCADE;
DROP TABLE IF EXISTS t3 CASCADE;
DROP TABLE IF EXISTS t4 CASCADE;
DROP TABLE IF EXISTS t5 CASCADE;
