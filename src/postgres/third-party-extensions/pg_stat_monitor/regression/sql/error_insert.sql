DROP TABLE IF EXISTS Company;

CREATE TABLE Company(
   ID INT PRIMARY KEY     NOT NULL,
   NAME TEXT    NOT NULL
);


CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
INSERT  INTO Company(ID, Name) VALUES (1, 'Percona'); 
INSERT  INTO Company(ID, Name) VALUES (1, 'Percona'); 

DROP TABLE IF EXISTS Company;
SELECT query, elevel, sqlcode, message FROM pg_stat_monitor ORDER BY query COLLATE "C",elevel;
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
