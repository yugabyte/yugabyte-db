
BASEPATH:=../../../

REGRESS := 1

PG_CONFIG ?= pg_config

# export pg_config for child make commands (check etc)
export PG_CONFIG

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

pg_major_version := $(shell $(PG_CONFIG) --version | awk -F' ' '{ print $$2 }' | awk -F'.' '{ print $$1 }')

EXTENSIONLOAD :=  --load-extension=tsm_system_rows --load-extension=pg_cron --load-extension=vector --load-extension=postgis --load-extension citus  --load-extension=documentdb_core --load-extension=rum --load-extension=documentdb --load-extension=documentdb_distributed

MAKEFILE_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))
export PATH := $(MAKEFILE_DIR)bin:$(PATH)
export PG_REGRESS_DIFF_OPTS = -dU10

export PGISOLATIONTIMEOUT = 60

.PHONY: check-basic check-dist-minimal

define common_test
	$(top_builddir)/src/test/regress/pg_regress --encoding=UTF8 --dlpath=$(BASEPATH) $(EXTENSIONLOAD) --temp-instance ./tmp --temp-config ./postgresql.conf --host localhost --port 58070 $(1) $(2) || (cat regression.diffs && false)
endef

define isolation_test
	$(top_builddir)/src/test/isolation/pg_isolation_regress --encoding=UTF8 --dlpath=$(BASEPATH) $(EXTENSIONLOAD) --temp-instance ./tmp --temp-config ./isolation_postgresql.conf --host localhost --port 58071 $(1) $(2) || (cat regression.diffs && false)
endef

check-basic: generate_version_schedule
	$(call common_test,--schedule=./basic_schedule --schedule=./log/basic_schedule_$(pg_major_version))

check-dist-minimal:
	$(call common_test,--schedule=./minimal_schedule, $(EXTRA_TESTS))

check-test-output:
	./validate_test_output.sh $(pg_major_version)

generate_version_schedule:
	@mkdir -p log
	@cp basic_schedule_core log/basic_schedule_$(pg_major_version)
	@./mutate_schedule.sh log/basic_schedule_$(pg_major_version) $(pg_major_version)


all: check-basic check-test-output
