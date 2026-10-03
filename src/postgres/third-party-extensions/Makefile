DIRS = \
	$(if $(filter 1,$(YB_SKIP_EXTRA_PG_EXTENSIONS)),,documentdb) \
	hypopg \
	mage \
	orafce \
	pg_cron \
	$(if $(findstring YB_ENABLE_YSQL_PG_DUCKDB_EXT,$(CXXFLAGS)),pg_duckdb,) \
	pg_hint_plan \
	$(if $(filter 1,$(YB_SKIP_PG_PARQUET_BUILD) $(YB_SKIP_EXTRA_PG_EXTENSIONS)),,pg_parquet) \
	pg_partman \
	pg_stat_monitor \
	pgaudit \
	pgtap \
	pgvector \
	postgresql-hll \
	postgresql_anonymizer \
	wal2json \

# YB: Extensions built with the vendored pgrx framework. They share one
# cargo-pgrx binary and one PGRX_HOME, both produced by the pgrx directory, so
# pgrx is built only when at least one consumer is being built.
#
# A new pgrx consumer must be registered here, or it falls back to building its
# own cargo-pgrx and reintroduces the duplicate build this ordering prevents.
YB_PGRX_CONSUMERS := $(strip $(filter pg_parquet,$(DIRS)) \
	$(if $(findstring YB_ENABLE_YSQL_DOCUMENTDB_EXT,$(CXXFLAGS)),$(filter documentdb,$(DIRS)),))
DIRS += $(if $(YB_PGRX_CONSUMERS),pgrx,)

BUILDDIRS = $(DIRS:%=build-%)
INSTALLDIRS = $(DIRS:%=install-%)
CLEANDIRS = $(DIRS:%=clean-%)

all: $(BUILDDIRS)
$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	$(MAKE) -C $(@:build-%=%)

# YB: cargo-pgrx must exist before a consumer installs. Without this ordering the
# consumers would each build their own copy, and under `make -j` they would race
# writing PGRX_HOME/config.toml. Only the install phase is ordered: every `all`
# target involved is a no-op, so plain `make` -- and `yb_build.sh compilecmds`,
# which never reaches install -- stays free of cargo.
$(YB_PGRX_CONSUMERS:%=install-%): install-pgrx

clean: $(CLEANDIRS)
$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

install: $(INSTALLDIRS)
$(INSTALLDIRS):
	$(MAKE) -C $(@:install-%=%) install

.PHONY: subdirs $(DIRS)
.PHONY: subdirs $(BUILDDIRS)
.PHONY: subdirs $(INSTALLDIRS)
.PHONY: subdirs $(CLEANDIRS)
.PHONY: all install clean
