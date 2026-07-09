DIRS = \
	documentdb \
	hypopg \
	mage \
	orafce \
	pg_cron \
	$(if $(findstring YB_ENABLE_YSQL_PG_DUCKDB_EXT,$(CXXFLAGS)),pg_duckdb,) \
	pg_hint_plan \
	$(if $(filter 1,$(YB_SKIP_PG_PARQUET_BUILD)),,pg_parquet) \
	pg_partman \
	pg_stat_monitor \
	pgaudit \
	pgtap \
	pgvector \
	postgresql-hll \
	postgresql_anonymizer \
	wal2json \

BUILDDIRS = $(DIRS:%=build-%)
INSTALLDIRS = $(DIRS:%=install-%)
CLEANDIRS = $(DIRS:%=clean-%)

all: $(BUILDDIRS)
$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	$(MAKE) -C $(@:build-%=%)

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
