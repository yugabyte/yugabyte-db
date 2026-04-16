CREATE EXTENSION pg_stat_monitor;

SELECT  name
        , setting
        , unit
        , context
        , vartype
        , source
        , min_val
        , max_val
        , enumvals
        , boot_val
        , reset_val
        , pending_restart 
FROM    pg_settings
WHERE   name     LIKE     'pg_stat_monitor.%'
ORDER
BY      name
COLLATE "C";

DROP EXTENSION pg_stat_monitor;
