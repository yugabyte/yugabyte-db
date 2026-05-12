
BASEPATH:=../../../

REGRESS := 1

PG_CONFIG ?= pg_config

# export pg_config for child make commands (check etc)
export PG_CONFIG

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

pg_major_version := $(shell $(PG_CONFIG) --version | awk -F' ' '{ print $$2 }' | awk -F'.' '{ print $$1 }' | awk -F'r' '{ print $$1 }')

EXTENSIONLOAD :=  --load-extension=tsm_system_rows --load-extension=pg_cron --load-extension=pageinspect --load-extension=vector --load-extension=postgis --load-extension=documentdb_core --load-extension=documentdb --load-extension=documentdb_extended_rum

MAKEFILE_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))
export PATH := $(MAKEFILE_DIR)/../regress/bin:$(PATH)
export PG_REGRESS_DIFF_OPTS = -dU10

.PHONY: check-basic check-minimal

define common_test
	$(top_builddir)/src/test/regress/pg_regress --encoding=UTF8 --dlpath=$(BASEPATH) $(EXTENSIONLOAD) --temp-instance ./tmp --temp-config ./postgresql.conf --host localhost --port 58070 $(1) $(2) || (cat regression.diffs && false)
endef

check-basic: generate_version_schedule
	$(call common_test,--schedule=./log/basic_schedule_$(pg_major_version))

check-minimal:
	$(call common_test,--schedule=./minimal_schedule, $(EXTRA_TESTS))

check-test-output:
	$(MAKEFILE_DIR)/../regress/validate_test_output.sh $(pg_major_version) $(MAKEFILE_DIR)

generate_version_schedule:
	@mkdir -p log
	@cp basic_schedule log/basic_schedule_$(pg_major_version)
	@$(MAKEFILE_DIR)/../regress/mutate_schedule.sh log/basic_schedule_$(pg_major_version) $(pg_major_version)


all: check-basic check-test-output
