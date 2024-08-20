EXTENSION = pg_helio_api

SQL_DEPDIR=.deps/sql
SQL_BUILDDIR=build/sql

template_sql_files = $(wildcard sql/*.sql)
generated_sql_files = $(patsubst %,build/%,$(template_sql_files))
DATA_built = $(generated_sql_files)
sql_file_inputs = $(wildcard sql/**/*.sql)

BUILD_SCRIPT_DIR = ../
OSS_SRC_DIR = ../
OSS_COMMON_SQL_HEADER = $(wildcard $(OSS_SRC_DIR)/common_header.sql)

# Extension defines
API_SCHEMA_NAME=helio_api
API_SCHEMA_INTERNAL_NAME=helio_api_internal
API_CATALOG_SCHEMA_NAME=helio_api_catalog
CORE_SCHEMA_NAME=helio_core
API_DATA_SCHEMA_NAME=helio_data
API_ADMIN_ROLE=helio_admin_role
POSTGIS_SCHEMA_NAME=public
EXTENSION_OBJECT_PREFIX=helio

# compilation configuration
MODULE_big = $(EXTENSION)

# TODO: Need to remove this with PG16 and expose only valid exports
ALLOW_DEFAULT_VISIBILITY=yes

include $(OSS_SRC_DIR)/Makefile.cflags

PG_CPPFLAGS += -I$(OSS_SRC_DIR)/pg_helio_core/include/

SOURCES = $(wildcard src/*.c) $(wildcard src/**/*.c)

OBJS = $(patsubst %.c,%.o,$(SOURCES))

DEBUG ?= no
ifeq ($(DEBUG),yes)
  PG_CPPFLAGS += -ggdb -O0 -g
  PG_CFLAGS += -ggdb -O0 -g
endif

SHLIB_LINK = $(libpq)

# clean up files generated to measure code coverage
EXTRA_CLEAN += $(addprefix src/,*.gcno *.gcda) $(addprefix src/**/,*.gcno *.gcda)
EXTRA_CLEAN += build/ .deps/

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

ifeq ($(ALLOW_DEFAULT_VISIBILITY),yes)
override LDFLAGS_SL += -fvisibility=default
override CFLAGS += -fvisibility=default
override CXXFLAGS += -fvisibility=default
endif

clean-sql:
	rm -rf .deps/ build/

check:
	$(MAKE) -C src/test all

check-minimal:
	$(MAKE) -C src/test/regress check-bson-minimal

citus-indent:
	citus_indent

analysis: citus-indent

check-regress:
	$(MAKE) -C src/test check-regress

# Before installing, trim any files extension*.sql files in the target install directory
install: trim_installed_data_files

trim_installed_data_files:
	rm -f $(DESTDIR)$(datadir)/$(datamoduledir)/$(EXTENSION)--*.sql

build-sql: $(generated_sql_files)

$(generated_sql_files): build/%: %
	@mkdir -p $(SQL_DEPDIR) $(SQL_BUILDDIR)
	@# -MF is used to store dependency files(.Po) in another directory for separation
	@# -MT is used to change the target of the rule emitted by dependency generation.
	@# -P is used to inhibit generation of linemarkers in the output from the preprocessor.
	@# -undef is used to not predefine any system-specific or GCC-specific macros.
	@# -imacros is used to specify a file that defines macros for the global context but its output is thrown away.
	@# `man cpp` for further information
	cpp -undef -w $(SQL_DEFINES) -imacros $(OSS_COMMON_SQL_HEADER) -P -MMD -MP -MF$(SQL_DEPDIR)/$(*F).Po -MT$@ $< > $@

include $(OSS_SRC_DIR)/Makefile.versions

src/helio_api_extension_version.o : build/include/metadata/extension_version.h