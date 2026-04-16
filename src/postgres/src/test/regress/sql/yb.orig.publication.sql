--
-- PUBLICATION
--
CREATE ROLE regress_publication_user LOGIN SUPERUSER;
SET SESSION AUTHORIZATION 'regress_publication_user';

CREATE TABLE testpub_tbl1 (id serial primary key, data text);
CREATE TABLE testpub_tbl2 (id int primary key, data text);
CREATE TABLE testpub_tbl3_nopk (foo int, bar int);
CREATE TABLE testpub_tbl4_nopk (foo int, bar int);

CREATE PUBLICATION testpub FOR ALL TABLES;

-- testpub_tbl3_nopk should get filtered out.
SELECT * FROM pg_publication_tables;

-- errors out since tables without pk are unsupported.
CREATE PUBLICATION testpub_explicit_list FOR TABLE testpub_tbl2, testpub_tbl3_nopk;

DROP PUBLICATION testpub;
DROP TABLE testpub_tbl1;
DROP TABLE testpub_tbl2;
DROP TABLE testpub_tbl3_nopk;
DROP TABLE testpub_tbl4_nopk;

RESET SESSION AUTHORIZATION;
DROP ROLE regress_publication_user;

-- Test yb_db_admin role can create publications with ALL TABLES
CREATE SCHEMA my_schema;
CREATE ROLE regress_yb_db_admin_user LOGIN;
SET SESSION AUTHORIZATION 'regress_yb_db_admin_user';
--should fail
CREATE PUBLICATION testpub_all_tables FOR ALL TABLES;
RESET SESSION AUTHORIZATION;
GRANT CREATE ON DATABASE yugabyte TO regress_yb_db_admin_user;
GRANT yb_db_admin TO regress_yb_db_admin_user WITH ADMIN OPTION;
SET SESSION AUTHORIZATION 'regress_yb_db_admin_user';
--should succeed
CREATE PUBLICATION testpub_all_tables FOR ALL TABLES;
RESET SESSION AUTHORIZATION;
DROP PUBLICATION testpub_all_tables;
RESET SESSION AUTHORIZATION;

CREATE ROLE regress_publication_superuser1 LOGIN SUPERUSER;
CREATE ROLE regress_yb_db_user1 LOGIN;
CREATE ROLE regress_yb_db_user2 LOGIN;
SET SESSION AUTHORIZATION 'regress_publication_superuser1';
CREATE PUBLICATION pub_all_tables1 FOR ALL TABLES;
--should succeed
ALTER PUBLICATION pub_all_tables1 OWNER TO regress_yb_db_user1;
SET SESSION AUTHORIZATION 'regress_yb_db_user1';
--should fail
ALTER PUBLICATION pub_all_tables1 OWNER TO regress_yb_db_user2;
RESET SESSION AUTHORIZATION;
CREATE PUBLICATION pub_all_tables2 FOR ALL TABLES;
CREATE ROLE regress_yb_db_admin_user2 LOGIN;
GRANT CREATE ON DATABASE yugabyte TO regress_yb_db_admin_user2;
GRANT yb_db_admin TO regress_yb_db_admin_user2 WITH ADMIN OPTION;
SET SESSION AUTHORIZATION 'regress_yb_db_admin_user';
--should succeed
ALTER PUBLICATION pub_all_tables2 OWNER TO regress_yb_db_admin_user2;
ALTER PUBLICATION pub_all_tables2 OWNER TO regress_yb_db_user1;
RESET SESSION AUTHORIZATION;

-- #22490: row filter with REPLICA IDENTITY DEFAULT should allow UPDATE/DELETE
-- when the filter only references replica identity (PK) columns.
CREATE TABLE yb_pub_rf_t(a int, b int, c text, PRIMARY KEY(a,c));
ALTER TABLE yb_pub_rf_t REPLICA IDENTITY DEFAULT;
SET client_min_messages = 'ERROR';
CREATE PUBLICATION yb_pub_rf FOR TABLE yb_pub_rf_t WHERE (a > 5 AND c = 'NSW');
RESET client_min_messages;

INSERT INTO yb_pub_rf_t VALUES (6, 106, 'NSW');
UPDATE yb_pub_rf_t SET b = 999 WHERE a = 6 AND c = 'NSW';
DELETE FROM yb_pub_rf_t WHERE a = 6 AND c = 'NSW';

DROP PUBLICATION yb_pub_rf;
DROP TABLE yb_pub_rf_t;

-- #22555: row filter with REPLICA IDENTITY CHANGE should allow UPDATE/DELETE
-- when the filter only references replica identity (PK) columns.
CREATE TABLE yb_pub_rf_change(a int, b int, c text, PRIMARY KEY(a,c));
ALTER TABLE yb_pub_rf_change REPLICA IDENTITY CHANGE;
SET client_min_messages = 'ERROR';
CREATE PUBLICATION yb_pub_rf_change FOR TABLE yb_pub_rf_change WHERE (a > 5 AND c = 'NSW');
RESET client_min_messages;

INSERT INTO yb_pub_rf_change VALUES (6, 106, 'NSW');
UPDATE yb_pub_rf_change SET b = 999 WHERE a = 6 AND c = 'NSW';
DELETE FROM yb_pub_rf_change WHERE a = 6 AND c = 'NSW';

DROP PUBLICATION yb_pub_rf_change;
DROP TABLE yb_pub_rf_change;
