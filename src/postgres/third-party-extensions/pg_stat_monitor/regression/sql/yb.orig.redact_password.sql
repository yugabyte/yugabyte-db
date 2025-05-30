CREATE EXTENSION IF NOT EXISTS pg_stat_monitor;
--
-- create / alter user
--
SELECT pg_stat_monitor_reset();
CREATE USER foo PASSWORD 'foo';
ALTER USER foo PASSWORD 'foo2';
CREATE ROLE bar PASSWORD 'bar';
ALTER ROLE bar PASSWORD 'bar2';

SELECT username, query FROM pg_stat_monitor ORDER BY query COLLATE "C";

DROP USER foo;
DROP ROLE bar;
