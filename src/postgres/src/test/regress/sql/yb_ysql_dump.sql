CREATE TABLE tbl1 (a SERIAL, b INT);
INSERT INTO tbl1 (b) VALUES (100);

CREATE TABLE tbl2 (a SERIAL);

CREATE TABLE tbl3(a INT, b INT, PRIMARY KEY(a ASC));

CREATE TABLE tbl4 (a INT, b INT, PRIMARY KEY(a HASH, b));

CREATE TABLE tbl5 (a INT PRIMARY KEY, b INT, c INT);
INSERT INTO tbl5 (a, b, c) VALUES (4, 7, 16);

CREATE TABLE tbl6 (a INT, PRIMARY KEY(a HASH));

CREATE TABLE tbl7 (a INT, b INT, c INT, d INT, PRIMARY KEY(b HASH, c));

CREATE TABLE tbl8 (a INT, b INT, c INT, d INT, PRIMARY KEY(a HASH, d));
CREATE INDEX tbl8_idx ON tbl8 ((b,c) HASH);
CREATE INDEX tbl8_idx2 ON tbl8 (a HASH, b);
CREATE INDEX tbl8_idx3 ON tbl8 (b ASC);
CREATE INDEX tbl8_idx4 ON tbl8 (b DESC);
CREATE INDEX tbl8_idx5 ON tbl8 (c);

CREATE TABLE tbl9 (a INT, b INT, c INT, PRIMARY KEY((a,b) HASH));

CREATE TABLE tbl10 (a INT, b INT, c INT, d INT, PRIMARY KEY((a,c) HASH, b));

CREATE TABLE tbl11 (a INT, b INT, c INT, PRIMARY KEY(a DESC, b ASC));

CREATE TABLE tbl12 (a INT, b INT, c INT, d INT, PRIMARY KEY(a ASC, d DESC, c DESC));

CREATE TABLE tbl13 (a INT, b INT, c INT, d INT, PRIMARY KEY((b,c) HASH));

CREATE USER tablegroup_test_user SUPERUSER;
CREATE USER rls_user NOLOGIN;

CREATE TABLE rls_public(k INT PRIMARY KEY, v TEXT);
CREATE TABLE rls_private(k INT PRIMARY KEY, v TEXT);

GRANT ALL ON rls_public TO public;
GRANT SELECT ON rls_private TO rls_user;

ALTER TABLE rls_public ENABLE ROW LEVEL SECURITY;
ALTER TABLE rls_private ENABLE ROW LEVEL SECURITY;
ALTER TABLE rls_private FORCE ROW LEVEL SECURITY;

CREATE POLICY p1 ON rls_public FOR ALL TO PUBLIC USING (k % 2 = 0);
CREATE POLICY p2 ON rls_private FOR INSERT WITH CHECK (k % 2 = 1);
CREATE POLICY p3 ON rls_private FOR UPDATE USING (k % 2 = 1);

CREATE TABLE chat_user("chatID" text NOT NULL, PRIMARY KEY("chatID"));

DROP USER IF EXISTS regress_rls_alice;
CREATE USER regress_rls_alice NOLOGIN;
SET SESSION AUTHORIZATION regress_rls_alice;
CREATE TABLE uaccount (pguser      name, seclv       int, PRIMARY KEY(pguser ASC));
ALTER TABLE uaccount ENABLE ROW LEVEL SECURITY;

CREATE POLICY account_policies ON uaccount USING (pguser = current_user);

SET SESSION AUTHORIZATION tablegroup_test_user;

CREATE TABLEGROUP grp1;
CREATE TABLEGROUP grp2;

CREATE TABLE tgroup_no_options_and_tgroup (a INT) TABLEGROUP grp1;
CREATE TABLE tgroup_one_option (a INT) WITH (autovacuum_enabled = true);
CREATE TABLE tgroup_one_option_and_tgroup (a INT) WITH (autovacuum_enabled = true) TABLEGROUP grp2;
CREATE TABLE tgroup_options (a INT) WITH (autovacuum_enabled=true, parallel_workers=2);
CREATE TABLE tgroup_options_and_tgroup (a INT) WITH (autovacuum_enabled=true, parallel_workers=2) TABLEGROUP grp2;
CREATE TABLE tgroup_after_options (a INT) TABLEGROUP grp1;
CREATE TABLE tgroup_in_between_options (a INT) WITH (autovacuum_enabled = true) TABLEGROUP grp1;
CREATE TABLE tgroup_empty_options (a INT);
BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
    UPDATE pg_class SET reloptions = ARRAY[]::TEXT[] WHERE relname = 'tgroup_empty_options';
    UPDATE pg_class SET reloptions = array_prepend('a=b', reloptions) WHERE relname = 'tgroup_after_options';
    UPDATE pg_class SET reloptions = array_prepend('a=b', reloptions) WHERE relname = 'tgroup_in_between_options';
COMMIT;
