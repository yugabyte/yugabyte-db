DIRS = \
	hypopg \
	orafce \
	pgaudit \
	pg_cron \
	pg_hint_plan \
	pg_stat_monitor \
	pgvector \
	postgresql-hll \
	postgresql_anonymizer \
	pg_partman \
	wal2json \
	pgtap \
	pg_parquet \

ifneq ($(findstring YB_ENABLE_YSQL_DOCUMENTDB_EXT, $(CXXFLAGS)),)
	DIRS += documentdb
endif

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
