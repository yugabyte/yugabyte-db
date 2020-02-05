\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;

SELECT set_config('search_path','partman, public',false);

SELECT plan(7);

-- Sanity check that new features get adding to definition dumping.
SELECT bag_eq(
  E'SELECT attname
    FROM pg_catalog.pg_attribute a
    JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
    WHERE c.relname = ''part_config''
      AND NOT attisdropped AND attnum > 0',
  ARRAY[
    'parent_table',
    'control',
    'partition_type',
    'partition_interval',
    'constraint_cols',
    'premake',
    'optimize_trigger',
    'optimize_constraint',
    'epoch',
    'inherit_fk',
    'retention',
    'retention_schema',
    'retention_keep_table',
    'retention_keep_index',
    'infinite_time_partitions',
    'datetime_string',
    'automatic_maintenance',
    'jobmon',
    'sub_partition_set_full',
    'undo_in_progress',
    'trigger_exception_handling',
    'upsert',
    'trigger_return_null',
    'template_table',
    'publications',
    'inherit_privileges',
    'constraint_valid'
  ]::TEXT[],
  'When adding a new column to part_config please ensure it is also added to the dump_partitioned_table_definition function'
);

-- Create a partman non-declarative partitioned table.
CREATE TABLE public.objects(id SERIAL PRIMARY KEY, t TEXT, created_at TIMESTAMP NOT NULL);
SELECT partman.create_parent('public.objects', 'created_at', 'partman', 'weekly', p_premake := 2);
-- Update config options you can't set at initial creation.
UPDATE partman.part_config
SET retention='5 weeks', retention_keep_table = 'f', infinite_time_partitions = 't'
WHERE parent_table = 'public.objects';

-- Test output "visually".
SELECT is(
  (SELECT partman.dump_partitioned_table_definition('public.objects')),
E'SELECT partman.create_parent(
	p_parent_table := ''public.objects'',
	p_control := ''created_at'',
	p_type := ''partman'',
	p_interval := ''weekly'',
	p_constraint_cols := NULL,
	p_premake := 2,
	p_automatic_maintenance := ''on'',
	p_inherit_fk := ''t'',
	p_epoch := ''none'',
	p_upsert := '''',
	p_publications := NULL,
	p_trigger_return_null := ''t'',
	p_template_table := NULL,
	p_jobmon := ''t''
	-- v_start_partition is intentionally ignored as there
	-- isn''t any obviously correct definition.
);
UPDATE partman.part_config SET
	optimize_trigger = 4,
	optimize_constraint = 30,
	retention = ''5 weeks'',
	retention_schema = NULL,
	retention_keep_table = ''f'',
	retention_keep_index = ''t'',
	infinite_time_partitions = ''t'',
	datetime_string = ''IYYY"w"IW'',
	sub_partition_set_full = ''f'',
	trigger_exception_handling = ''f'',
	inherit_privileges = ''t'',
	constraint_valid = ''t''
WHERE parent_table = ''public.objects'';'
);

-- Test end to end:
-- 1. Capture the current config.
SELECT part_config AS objects_part_config FROM partman.part_config WHERE parent_table = 'public.objects'
\gset
SELECT partman.dump_partitioned_table_definition('public.objects') AS var_sql
\gset
-- 2. Remove partitioning and recreate table.
SELECT partman.undo_partition('public.objects', p_keep_table := false);
DROP TABLE public.objects;
CREATE TABLE public.objects(id SERIAL PRIMARY KEY, t TEXT, created_at TIMESTAMP NOT NULL);
SELECT is((SELECT 1 FROM partman.part_config WHERE parent_table = 'public.objects'), NULL);
-- 3. Run dumped config.
SELECT :'var_sql'
\gexec
-- 4. Check the current config (it should match step 1).
SELECT row_eq(
  'SELECT * FROM partman.part_config WHERE parent_table = ''public.objects''',
  (:'objects_part_config')::partman.part_config
);


-- Create a partman declarative partitioned table.
CREATE TABLE public.declarative_objects(
  id SERIAL,
  t TEXT,
  created_at TIMESTAMP NOT NULL
) PARTITION BY RANGE (created_at);
SELECT partman.create_parent('public.declarative_objects', 'created_at', 'native', 'weekly', p_premake := 2, p_start_partition := (NOW() - '4 weeks'::INTERVAL)::TEXT);
-- Update config options you can't set at initial creation.
UPDATE partman.part_config
SET retention='5 weeks', retention_keep_table = 'f', infinite_time_partitions = 't', constraint_valid = 'f', inherit_privileges = 't'
WHERE parent_table = 'public.declarative_objects';

-- Test output "visually" (with p_ignore_template_table = true).
SELECT partman.dump_partitioned_table_definition('public.declarative_objects', p_ignore_template_table := true);
--
-- -- Test output "visually" (with default p_ignore_template_table = false).
SELECT is(
  (SELECT partman.dump_partitioned_table_definition('public.declarative_objects')),
E'SELECT partman.create_parent(
	p_parent_table := ''public.declarative_objects'',
	p_control := ''created_at'',
	p_type := ''native'',
	p_interval := ''7 days'',
	p_constraint_cols := NULL,
	p_premake := 2,
	p_automatic_maintenance := ''on'',
	p_inherit_fk := ''t'',
	p_epoch := ''none'',
	p_upsert := '''',
	p_publications := NULL,
	p_trigger_return_null := ''t'',
	p_template_table := ''partman.template_public_declarative_objects'',
	p_jobmon := ''t''
	-- v_start_partition is intentionally ignored as there
	-- isn''t any obviously correct definition.
);
UPDATE partman.part_config SET
	optimize_trigger = 4,
	optimize_constraint = 30,
	retention = ''5 weeks'',
	retention_schema = NULL,
	retention_keep_table = ''f'',
	retention_keep_index = ''t'',
	infinite_time_partitions = ''t'',
	datetime_string = ''IYYY"w"IW'',
	sub_partition_set_full = ''f'',
	trigger_exception_handling = ''f'',
	inherit_privileges = ''t'',
	constraint_valid = ''f''
WHERE parent_table = ''public.declarative_objects'';'
);

-- Test end to end (with p_ignore_template_table = true):
-- 1. Capture the current config.
SELECT part_config AS declarative_objects_part_config FROM partman.part_config WHERE parent_table = 'public.declarative_objects'
\gset
SELECT partman.dump_partitioned_table_definition('public.declarative_objects', p_ignore_template_table := true) AS var_sql
\gset
-- 2. Remove partitioning and recreate table.
CREATE TABLE public.old_declarative_objects(id SERIAL, t TEXT, created_at TIMESTAMP NOT NULL);
SELECT partman.undo_partition('public.declarative_objects', p_keep_table := false, p_target_table := 'public.old_declarative_objects');
DROP TABLE public.declarative_objects;
CREATE TABLE public.declarative_objects(
  id SERIAL,
  t TEXT,
  created_at TIMESTAMP NOT NULL
) PARTITION BY RANGE (created_at);
-- 3. Run dumped config.
SELECT :'var_sql'
\gexec
-- 4. Check the current config (it should match step 1).
SELECT row_eq(
  'SELECT * FROM partman.part_config WHERE parent_table = ''public.declarative_objects''',
  (:'declarative_objects_part_config')::partman.part_config
);

-- Test end to end (with default p_ignore_template_table = false):
-- 1. Capture the current config.
SELECT part_config AS declarative_objects_part_config FROM partman.part_config WHERE parent_table = 'public.declarative_objects'
\gset
SELECT partman.dump_partitioned_table_definition('public.declarative_objects') AS var_sql
\gset
-- 2. Remove partitioning and recreate table.
SELECT partman.undo_partition('public.declarative_objects', p_keep_table := false, p_target_table := 'public.old_declarative_objects');
DROP TABLE public.declarative_objects;
CREATE TABLE public.declarative_objects(
  id SERIAL,
  t TEXT,
  created_at TIMESTAMP NOT NULL
) PARTITION BY RANGE (created_at);
-- 3. Run dumped config (after creating template table).
CREATE TABLE partman.template_public_declarative_objects(
  id SERIAL,
  t TEXT,
  created_at TIMESTAMP NOT NULL
);
SELECT :'var_sql'
\gexec
-- 4. Check the current config (it should match step 1).
SELECT row_eq(
  'SELECT * FROM partman.part_config WHERE parent_table = ''public.declarative_objects''',
  (:'declarative_objects_part_config')::partman.part_config
);

SELECT * FROM finish();

ROLLBACK;
