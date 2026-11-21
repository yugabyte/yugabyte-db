--
-- A DDL's catalog reads see its catalog writes of earlier commands only, as
-- PostgreSQL's catalog snapshot does, also when the caches are rebuilt in the
-- middle of the DDL.  yb_test_invalidate_caches_after_catalog_delete rebuilds
-- them after each catalog row delete, before the row's index entries are
-- deleted, as a full cache refresh reached from AcceptInvalidationMessages
-- would.
--
CREATE TABLE cid_ddl (k int PRIMARY KEY, c int DEFAULT 0, d int DEFAULT 1,
  e int DEFAULT 2);
SET yb_test_invalidate_caches_after_catalog_delete = on;
ALTER TABLE cid_ddl ALTER COLUMN c DROP DEFAULT;
ALTER TABLE cid_ddl DROP COLUMN d;
BEGIN;
ALTER TABLE cid_ddl ALTER COLUMN e DROP DEFAULT;
ALTER TABLE cid_ddl ALTER COLUMN e SET DEFAULT 3;
COMMIT;
RESET yb_test_invalidate_caches_after_catalog_delete;
INSERT INTO cid_ddl (k) VALUES (1);
SELECT * FROM cid_ddl;

CREATE TABLE cid_ddl2 (k int PRIMARY KEY, c int DEFAULT 0);
SET yb_test_invalidate_caches_after_catalog_delete = on;
DROP TABLE cid_ddl2;
RESET yb_test_invalidate_caches_after_catalog_delete;
SELECT count(*) FROM pg_class WHERE relname = 'cid_ddl2';

DROP TABLE cid_ddl;
