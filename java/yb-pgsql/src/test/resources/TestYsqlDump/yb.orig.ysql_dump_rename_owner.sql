-- Source schema for the --rename-owner flag test.
--
-- The source DB rename_owner_src_db is owned by rename_owner_src_role.
-- The dump runs with --rename-owner=rename_owner_tgt_role, so every
-- "OWNER TO rename_owner_src_role" clause in the captured output should
-- become "OWNER TO rename_owner_tgt_role" while clauses for
-- rename_owner_other_role must stay untouched. The role-existence guard
-- emitted under --dump-role-checks must also reference the new owner.
--
-- Two of each object kind are created: one owned by the source DB owner
-- (must be rewritten) and one owned by an unrelated role (must NOT be
-- rewritten).

CREATE ROLE rename_owner_src_role;
CREATE ROLE rename_owner_tgt_role;
CREATE ROLE rename_owner_other_role;

CREATE DATABASE rename_owner_src_db OWNER rename_owner_src_role;
\c rename_owner_src_db

-- SPLIT INTO N TABLETS is set explicitly on every HASH-partitioned table so
-- the dump output is independent of the cluster's default tablet count
-- (which differs between yb-ctl and the Java mini-cluster).

CREATE TABLE t_src (k int PRIMARY KEY) SPLIT INTO 3 TABLETS;
ALTER TABLE  t_src OWNER TO rename_owner_src_role;

CREATE SEQUENCE seq_src;
ALTER SEQUENCE seq_src OWNER TO rename_owner_src_role;

CREATE FUNCTION fn_src() RETURNS int LANGUAGE sql AS 'SELECT 1';
ALTER FUNCTION fn_src() OWNER TO rename_owner_src_role;

CREATE TABLE t_other (k int PRIMARY KEY) SPLIT INTO 3 TABLETS;
ALTER TABLE  t_other OWNER TO rename_owner_other_role;

CREATE SEQUENCE seq_other;
ALTER SEQUENCE seq_other OWNER TO rename_owner_other_role;

CREATE FUNCTION fn_other() RETURNS int LANGUAGE sql AS 'SELECT 2';
ALTER FUNCTION fn_other() OWNER TO rename_owner_other_role;
