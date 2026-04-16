.PHONY: all clean install check check-minimal check-regress build-sql generate_errors_file generate_external_error_mapping_file clean-sql trim_installed_data_files citus-indent analysis
EXTENSION = documentdb_core
MODULE_big = pg_$(EXTENSION)

SQL_DEPDIR=.deps/sql
SQL_BUILDDIR=build/sql

template_sql_files = $(wildcard sql/*.sql)
generated_sql_files = $(patsubst %,build/%,$(template_sql_files))
DATA_built = $(generated_sql_files)
sql_file_inputs = $(wildcard sql/**/*.sql)

DOCDB_CORE_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))
BUILD_SCRIPT_DIR = ../
OSS_SRC_DIR = $(DOCDB_CORE_DIR)/../
OSS_COMMON_SQL_HEADER = $(wildcard $(OSS_SRC_DIR)/common_header.sql)

# Extension configuration
SKIP_API_SCHEMA=yes
CORE_SCHEMA_NAME=documentdb_core
EXTENSION_OBJECT_PREFIX=documentdb

# TODO: Need to remove this with PG16 and expose only valid exports
ALLOW_DEFAULT_VISIBILITY=yes

# compilation configuration
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

generate_errors_file: $(DOCDB_CORE_DIR)include/utils/documentdb_errors.h $(OSS_SRC_DIR)/documentdb_errors.csv

generate_external_error_mapping_file: generate_errors_file $(DOCDB_CORE_DIR)include/utils/external_error_mapping.csv 

all: generate_external_error_mapping_file

$(DOCDB_CORE_DIR)include/utils/documentdb_errors.h: $(OSS_SRC_DIR)/scripts/generate_error_values.sh $(OSS_SRC_DIR)/documentdb_errors.csv
	mkdir -p $(DOCDB_CORE_DIR)/build
	mkdir -p $(DOCDB_CORE_DIR)/include/utils
	$(OSS_SRC_DIR)/scripts/generate_error_values.sh $(OSS_SRC_DIR)/documentdb_errors.csv $(DOCDB_CORE_DIR)include/utils/documentdb_errors.h $(DOCDB_CORE_DIR)include/utils/documentdb_codes.txt documentdb

$(DOCDB_CORE_DIR)include/utils/documentdb_codes.txt: $(DOCDB_CORE_DIR)include/utils/documentdb_errors.h

$(DOCDB_CORE_DIR)include/utils/external_error_mapping.csv: $(OSS_SRC_DIR)/scripts/generate_external_error_mapping_file.sh $(OSS_SRC_DIR)/error_mappings.csv $(DOCDB_CORE_DIR)include/utils/documentdb_codes.txt
	$(OSS_SRC_DIR)/scripts/generate_external_error_mapping_file.sh $(DOCDB_CORE_DIR)include/utils/documentdb_codes.txt $(OSS_SRC_DIR)/error_mappings.csv \
		$(DOCDB_CORE_DIR)include/utils/external_error_mapping.csv 

$(generated_sql_files): build/%: %
	@mkdir -p $(SQL_DEPDIR) $(SQL_BUILDDIR)
	@# -MF is used to store dependency files(.Po) in another directory for separation
	@# -MT is used to change the target of the rule emitted by dependency generation.
	@# -P is used to inhibit generation of linemarkers in the output from the preprocessor.
	@# -undef is used to not predefine any system-specific or GCC-specific macros.
	@# -imacros is used to specify a file that defines macros for the global context but its output is thrown away.
	@# `man cpp` for further information
	cpp -undef -w $(SQL_DEFINES) -imacros $(OSS_COMMON_SQL_HEADER) -P -MMD -MP -MF$(SQL_DEPDIR)/$(*F).Po -MT$@ $< > $@