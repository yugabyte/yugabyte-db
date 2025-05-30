EXTENSION = documentdb
MODULE_big = pg_$(EXTENSION)

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
API_SCHEMA_NAME=documentdb_api
API_SCHEMA_NAME_V2=documentdb_api
API_SCHEMA_INTERNAL_NAME=documentdb_api_internal
API_SCHEMA_INTERNAL_NAME_V2=documentdb_api_internal
API_CATALOG_SCHEMA_NAME=documentdb_api_catalog
API_CATALOG_SCHEMA_NAME_V2=documentdb_api_catalog
CORE_SCHEMA_NAME=documentdb_core
API_DATA_SCHEMA_NAME=documentdb_data
API_ADMIN_ROLE=documentdb_admin_role
API_READONLY_ROLE=documentdb_readonly_role
POSTGIS_SCHEMA_NAME=public
EXTENSION_OBJECT_PREFIX=documentdb
API_GUC_PREFIX=documentdb

# TODO: Need to remove this with PG16 and expose only valid exports
ALLOW_DEFAULT_VISIBILITY=yes

USE_DOCUMENTDB_CORE = 1
include $(OSS_SRC_DIR)/Makefile.cflags
SOURCES = $(wildcard src/*.c) $(wildcard src/**/*.c)

OBJS = $(patsubst %.c,%.o,$(SOURCES))

DEBUG ?= no
ifeq ($(DEBUG),yes)
  PG_CPPFLAGS += -ggdb -O0 -g
  PG_CFLAGS += -ggdb -O0 -g
endif

SHLIB_LINK = $(libpq)

include $(OSS_SRC_DIR)/Makefile.global

clean-sql:
	rm -rf .deps/ build/

check:
	$(MAKE) -C src/test all

check-minimal:
	$(MAKE) -C src/test/regress check-bson-minimal

check-documentdb-minimal:
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

src/documentdb_extension_version.o : build/include/metadata/documentdb_extension_version.h