CREATE USER su WITH SUPERUSER;

SET ROLE su;
CREATE EXTENSION pg_stat_monitor;

CREATE USER u1;
CREATE USER u2;

GRANT ALL ON SCHEMA public TO u1;
GRANT ALL ON SCHEMA public TO u2;

SET ROLE su;
SELECT pg_stat_monitor_reset();

SET ROLE u1;
SELECT pg_stat_monitor_reset();
CREATE TABLE t1 (a int);
SELECT * FROM t1;

SET ROLE u2;
CREATE TABLE t2 (a int);
SELECT * FROM t2;
DROP TABLE t2;

SET ROLE su;

DROP OWNED BY u2;
DROP USER u2;

SELECT username, query FROM pg_stat_monitor WHERE username != 'postgres' ORDER BY username, query COLLATE "C"; -- YB: add username filter to avoid catalog version update queries
SELECT pg_stat_monitor_reset();

DROP TABLE t1;
DROP OWNED BY u1;
DROP USER u1;

DROP EXTENSION pg_stat_monitor;

SET ROLE NONE;

DROP OWNED BY su;
DROP USER su;
