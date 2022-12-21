CREATE EXTENSION pg_stat_monitor;

\x

SELECT  name
        , setting
        , unit
        , category
        , short_desc
        , extra_desc
        , context
        , vartype
        , source
        , min_val
        , max_val
        , enumvals
        , boot_val
        , reset_val
        , sourcefile
        , sourceline
        , pending_restart 
FROM    pg_settings
WHERE   name     LIKE     'pg_stat_monitor.%'
        AND name NOT LIKE 'pg_stat_monitor.pgsm_track_planning'
ORDER
BY      name
COLLATE "C";

\x

DROP EXTENSION pg_stat_monitor;
