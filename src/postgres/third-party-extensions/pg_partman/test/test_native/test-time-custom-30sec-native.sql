-- ########## TIME CUSTOM TESTS NATIVE ##########
  -- Additional tests: Inherit privileges
-- May fail when run in the first or second 30 seconds of the minute due to rounding down to the nearest minute.
-- If it does, wait until the next block of 30 seconds starts and try again.
-- If it is failing no matter when it is run, please create an issue on Github with a log of your result when running with "pg_prove -ovf" 


\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(146);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text, col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.time_taptest_table_template (LIKE partman_test.time_taptest_table INCLUDING ALL);
-- TODO change back to parent table when fixed in future pg version
ALTER TABLE partman_test.time_taptest_table_template ADD PRIMARY KEY (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

DO $pg11_objects_check$
BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
    -- Create on parent table
    CREATE INDEX ON partman_test.time_taptest_table (col3);
ELSE
    -- Create on template table
    CREATE INDEX ON partman_test.time_taptest_table_template (col3);
END IF;
END $pg11_objects_check$;


GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', '30 seconds', p_template_table := 'partman_test.time_taptest_table_template');
-- Must run_maintenance because when interval time is between 1 hour and 1 minute, the first partition name done by above is always the nearest hour rounded down 
SELECT run_maintenance();
UPDATE part_config SET inherit_privileges = TRUE;
SELECT reapply_privileges('partman_test.time_taptest_table');


INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 
                'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
/* extra previous tables may exist due to new rounding down of the hour. Test left here for manual checking 
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'150 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
*/

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

--TODO REMOVE SELECT partition_data_time('partman_test.time_taptest_table');

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '30 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '60 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '90 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '120 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '30 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '60 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '90 secs'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '120 secs'::interval);

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '150 secs'::interval);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

GRANT DELETE ON partman_test.time_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table OWNER TO partman_owner;

UPDATE part_config SET premake = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '180 secs'::interval);

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'360 mins'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'360 mins'::interval, 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE','DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE','DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

-- TODO Change this to check the default partition when that is added
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '600 mins'::interval);
-- SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_taptest_table');
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT drop_partition_time('partman_test.time_taptest_table', '90 secs', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'120 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');

UPDATE part_config SET retention = '60 secs'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT has_table('partman_retention_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'90 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' got moved to new schema');

SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM partman_test.undo_taptest', ARRAY[118], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0), 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'30 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)-'60 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'30 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'60 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'90 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'120 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'150 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'180 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'210 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'240 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'270 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'300 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_'||to_char(date_trunc('minute', CURRENT_TIMESTAMP) + 
                '30sec'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0)+'330 secs'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist');

SELECT hasnt_table('partman_test', 'time_taptest_table_template', 'Check that template table was dropped');

SELECT * FROM finish();
ROLLBACK;
