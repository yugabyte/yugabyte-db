project('orafce', ['c'], version: '4.16.6')

pg_config = find_program('pg_config')

bindir = run_command(pg_config, '--bindir', check: true).stdout().strip()
includedir_server = run_command(pg_config, '--includedir-server', check: true).stdout().strip()
includedir = run_command(pg_config, '--includedir', check: true).stdout().strip()
pkglibdir = run_command(pg_config, '--pkglibdir', check: true).stdout().strip()
sharedir = run_command(pg_config, '--sharedir', check: true).stdout().strip()
libdir = run_command(pg_config, '--libdir', check: true).stdout().strip()

versionstring = run_command(pg_config, '--version', check: true).stdout().strip()
pg_version = versionstring.split(' ')[1]

if pg_version.endswith('devel')
  pg_version_arr = [pg_version.split('devel')[0], '0']
elif pg_version.contains('beta')
  pg_version_arr = [pg_version.split('beta')[0], '0']
elif pg_version.contains('rc')
  pg_version_arr = [pg_version.split('rc')[0], '0']
else
  pg_version_arr = pg_version.split('.')
endif

pg_version_major = pg_version_arr[0].to_int()
pg_version_minor = pg_version_arr[1].to_int()
pg_version_num = (pg_version_major * 10000) + pg_version_minor

module_version_minor = meson.project_version()
module_version_arr = module_version_minor.split('.')
module_version = module_version_arr[0] + '.' + module_version_arr[1]
module_name = meson.project_name()

sources = files(
  'aggregate.c',
  'alert.c',
  'dbms_assert.c',
  'convert.c',
  'datefce.c',
  'dbms_sql.c',
  'file.c',
  'charlen.c',
  'charpad.c',
  'magic.c',
  'math.c',
  'nvarchar2.c',
  'orafce.c',
  'others.c',
  'parse_keyword.c',
  'pipe.c',
  'plunit.c',
  'plvdate.c',
  'plvlex.c',
  'plvstr.c',
  'plvsubst.c',
  'putline.c',
  'random.c',
  'regexp.c',
  'replace_empty_string.c',
  'shmmc.c',
  'sqlparse.c',
  'utility.c',
  'varchar2.c',
)

data = [
  module_name + '.control',
  'orafce--3.2--3.3.sql',
  'orafce--3.3--3.4.sql',
  'orafce--3.4--3.5.sql',
  'orafce--3.5--3.6.sql',
  'orafce--3.6--3.7.sql',
  'orafce--3.7--3.8.sql',
  'orafce--3.8--3.9.sql',
  'orafce--3.9--3.10.sql',
  'orafce--3.10--3.11.sql',
  'orafce--3.11--3.12.sql',
  'orafce--3.12--3.13.sql',
  'orafce--3.13--3.14.sql',
  'orafce--3.14--3.15.sql',
  'orafce--3.15--3.16.sql',
  'orafce--3.16--3.17.sql',
  'orafce--3.17--3.18.sql',
  'orafce--3.18--3.19.sql',
  'orafce--3.19--3.20.sql',
  'orafce--3.20--3.21.sql',
  'orafce--3.21--3.22.sql',
  'orafce--3.22--3.23.sql',
  'orafce--3.23--3.24.sql',
  'orafce--3.24--3.25.sql',
  'orafce--3.25--4.0.sql',
  'orafce--4.0--4.1.sql',
  'orafce--4.1--4.2.sql',
  'orafce--4.2--4.3.sql',
  'orafce--4.3--4.4.sql',
  'orafce--4.4--4.5.sql',
  'orafce--4.5--4.6.sql',
  'orafce--4.6--4.7.sql',
  'orafce--4.7--4.8.sql',
  'orafce--4.8--4.9.sql',
  'orafce--4.9--4.10.sql',
  'orafce--4.10--4.11.sql',
  'orafce--4.11--4.12.sql',
  'orafce--4.12--4.13.sql',
  'orafce--4.13--4.14.sql',
  'orafce--4.14--4.15.sql',
  'orafce--4.15--4.16.sql',
   module_name + '--' + module_version + '.sql'
]

tests = [
  'init',
  'orafce',
  'orafce2',
  'dbms_output',
  'dbms_utility',
  'files',
  'varchar2',
  'nvarchar2',
  'aggregates',
  'nlssort',
  'dbms_random',
  'regexp_func',
  'dbms_sql',
]

compilerName = meson.get_compiler('c').get_id()

if meson.get_compiler('c').get_id() == 'msvc'
  incdir = [includedir_server / 'port/win32_msvc',
            includedir_server / 'port/win32',
            includedir_server,
            includedir]
  postgres_lib = meson.get_compiler('c').find_library(
                                           'postgres',
                                            dirs: libdir,
                                            static: true,
                                            required: true
                                         )
else
  incdir = [ includedir_server ]
  postgres_lib = []
endif

module_lib = shared_module(
  module_name,
  sources,
  include_directories: incdir,
  install: true,
  install_dir: pkglibdir,
  name_prefix: '',
  dependencies: postgres_lib,
)

install_data(data, install_dir: sharedir / 'extension')

fs = import('fs')
bindistdir = fs.expanduser('~' / module_name / module_name + '-' +
  '-'.join( [ module_version_minor,
              target_machine.cpu(),
              target_machine.system() ] ))

lib_name = fs.name(module_lib.full_path())
lib_suffix = lib_name.split('.')[1]

python_exe = find_program('python3', 'python')

custom_target('bindist',
  depends: module_lib,
  input: module_lib,
  output: 'bindist',
  command: [ python_exe,
             meson.current_source_dir() / 'install_bindist.py',
             meson.current_source_dir(),
             bindistdir,
             module_lib.full_path(),
             fs.stem(lib_name) + '_' + pg_version_major.to_string() + '.' + lib_suffix,
             data ],
  install: false,
  build_by_default: false)

pg_regress = find_program(
  'pg_regress',
  dirs: [pkglibdir / 'pgxs/src/test/regress']
)

test('regress',
  pg_regress,
  args: ['--bindir', bindir,
         '--inputdir', meson.current_source_dir(),
         '--encoding', 'utf8',
         '--schedule', meson.current_source_dir() / 'parallel_schedule',
        ] + tests,
)
