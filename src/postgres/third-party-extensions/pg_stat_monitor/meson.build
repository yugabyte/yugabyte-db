# Copyright (c) 2022-2023, PostgreSQL Global Development Group

pg_stat_monitor_sources = files(
  'pg_stat_monitor.c',
)

pg_stat_monitor = shared_module('pg_stat_monitor',
  pg_stat_monitor_sources,
  kwargs: contrib_mod_args + {
    'dependencies': contrib_mod_args['dependencies'],
  },
)
contrib_targets += pg_stat_monitor

install_data(
  'pg_stat_monitor.control',
  'pg_stat_monitor--2.0.sql',
  'pg_stat_monitor--1.0--2.0.sql',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'pg_stat_monitor',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'regress': {
    'sql': [
      'application_name',
      'application_name_unique',
      'basic',
      'cmd_type',
      'counters',
      'database',
      'error_insert',
      'error',
      'functions',
      'guc',
      'histogram',
      'pgsqm_query_id',
      'relations',
      'rows',
      'state',
      'tags',
      'top_query',
      'user',
      'version'
    ],
    'regress_args': ['--temp-config', files('pg_stat_monitor.conf')],
    # Disabled because these tests require
    # "shared_preload_libraries=pg_stat_monitor", which typical
    # runningcheck users do not have (e.g. buildfarm clients).
    'runningcheck': false,
  },
}
