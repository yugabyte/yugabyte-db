CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Test dropping mix of temp and regular tables.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
CREATE TABLE foo(i int PRIMARY KEY);
DROP TABLE temp_foo, foo; -- should fail
DROP TABLE foo, temp_foo; -- should fail
DROP TABLE temp_foo;
DROP TABLE foo;

-- Test CREATE TABLE AS / SELECT INTO reading temp relations (#34017).
CREATE TEMP TABLE temp_src(i int PRIMARY KEY);
INSERT INTO temp_src VALUES (1), (2);
CREATE TABLE perm_src(i int PRIMARY KEY);
CREATE TEMP VIEW temp_view AS SELECT i FROM perm_src;
CREATE TABLE ctas_from_temp AS SELECT * FROM temp_src; -- should fail
SELECT * INTO ctas_from_temp FROM temp_src; -- should fail
CREATE TABLE ctas_from_temp AS SELECT i FROM (SELECT i FROM temp_src) sub; -- should fail
CREATE TABLE ctas_from_temp AS WITH t AS (SELECT i FROM temp_src) SELECT i FROM t; -- should fail
CREATE TABLE ctas_from_temp AS SELECT p.i FROM perm_src p JOIN temp_src t USING (i); -- should fail
CREATE TABLE ctas_from_temp AS SELECT * FROM temp_view; -- should fail
CREATE TEMP TABLE temp_ctas AS SELECT * FROM temp_src; -- ok: temp target is not replicated
SELECT * INTO TEMP temp_select_into FROM temp_src; -- ok: temp target is not replicated
CREATE TABLE ctas_from_perm AS SELECT * FROM perm_src; -- ok
SELECT * INTO select_into_from_perm FROM perm_src; -- ok
DROP TABLE temp_ctas;
DROP TABLE temp_select_into;
DROP TABLE ctas_from_perm;
DROP TABLE select_into_from_perm;
DROP VIEW temp_view;
DROP TABLE perm_src;
DROP TABLE temp_src;

SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
