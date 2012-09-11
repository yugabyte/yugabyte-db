-- Run this script from the pg_partman top folder with psql command line tool to install pg_partman. 

DROP SCHEMA IF EXISTS part CASCADE;
CREATE SCHEMA part;

\i sql/types/types.sql
\i sql/tables/tables.sql
\i sql/functions/create_id_function.sql
\i sql/functions/create_id_partition.sql
\i sql/functions/create_next_time_partition.sql
\i sql/functions/create_parent.sql
\i sql/functions/create_time_function.sql
\i sql/functions/create_time_partition.sql
\i sql/functions/create_trigger.sql
--\i sql/functions/
\i sql/functions/run_maintenance.sql

