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
