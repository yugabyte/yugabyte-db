---
--- Verify renaming on temp tables
---
CREATE TEMP TABLE temp_table(a int primary key, b int);
CREATE INDEX temp_table_b_idx ON temp_table(b);
ALTER INDEX temp_table_pkey RENAME TO temp_table_pkey_new;
ALTER INDEX temp_table_b_idx RENAME TO temp_table_b_idx_new;

---
--- Verify yb_db_admin role can ALTER table
---
CREATE TABLE table_other(a int, b int);
CREATE INDEX index_table_other ON table_other(a);
SET SESSION AUTHORIZATION yb_db_admin;
ALTER TABLE table_other RENAME to table_new;
ALTER TABLE table_new OWNER TO regress_alter_table_user1;
ALTER TABLE pg_database RENAME TO test; -- should fail
ALTER TABLE pg_tablespace OWNER TO regress_alter_table_user1; -- should fail
---
--- Verify yb_db_admin role can ALTER index
---
ALTER INDEX index_table_other RENAME TO index_table_other_new;
RESET SESSION AUTHORIZATION;
