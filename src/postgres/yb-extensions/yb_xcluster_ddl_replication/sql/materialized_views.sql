CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();

CREATE TABLE products (
    id INT PRIMARY KEY,
    category TEXT,
    price NUMERIC
);

CREATE MATERIALIZED VIEW product_summary AS
  SELECT category, COUNT(*) as item_count
  FROM products
  GROUP BY category;

COMMENT ON MATERIALIZED VIEW product_summary
  IS 'Stores the total count of products per category.';

ALTER MATERIALIZED VIEW product_summary RENAME TO product_inventory;

REFRESH MATERIALIZED VIEW product_inventory;

-- In-place refresh keeps the relfilenode and is replicated as DML, so the
-- target has to run in the same mode. Verify the effective value is recorded.
INSERT INTO products VALUES (1, 'a', 1.0);
SET yb_refresh_matview_in_place = true;
REFRESH MATERIALIZED VIEW product_inventory;

-- An explicit off is recorded the same way as the default.
SET yb_refresh_matview_in_place = false;
REFRESH MATERIALIZED VIEW product_inventory;
RESET yb_refresh_matview_in_place;

DROP MATERIALIZED VIEW product_inventory;

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;


-- Test dropping materialized views using other DROP commands
-- DROP SCHEMA
CREATE SCHEMA IF NOT EXISTS permanent_schema;
CREATE TABLE permanent_schema.my_table (id INT);
CREATE MATERIALIZED VIEW permanent_schema.my_mview AS SELECT 'hello' AS greeting;
DROP SCHEMA permanent_schema CASCADE;

-- DROP ROLE
CREATE ROLE drop_me_role;
SET ROLE drop_me_role;
CREATE TEMP TABLE my_temp_object (id INT);
CREATE MATERIALIZED VIEW my_mview AS SELECT 'hello' AS greeting;
RESET ROLE;
DROP OWNED BY drop_me_role; -- fail due to temp object

SET ROLE drop_me_role;
DROP TABLE my_temp_object;
CREATE TABLE real_table (id INT);
RESET ROLE;
DROP OWNED BY drop_me_role; -- should succeed now

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
select * from TEST_verify_replicated_ddls();
