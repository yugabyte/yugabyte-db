EXTENSION = pg_hypo
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
TESTS        = $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test
DOCS         = $(wildcard README.md)

PG_CONFIG = pg_config

MODULE_big = pg_hypo
OBJS = pg_hypo.o

all:

release-zip: all
	git archive --format zip --prefix=pg_hypo-${EXTVERSION}/ --output ./pg_hypo-${EXTVERSION}.zip HEAD
	unzip ./pg_hypo-$(EXTVERSION).zip
	rm ./pg_hypo-$(EXTVERSION).zip
	rm ./pg_hypo-$(EXTVERSION)/.gitignore
	sed -i -e "s/__VERSION__/$(EXTVERSION)/g"  ./pg_hypo-$(EXTVERSION)/META.json
	zip -r ./pg_hypo-$(EXTVERSION).zip ./pg_hypo-$(EXTVERSION)/
	rm ./pg_hypo-$(EXTVERSION) -rf


DATA = $(wildcard *--*.sql)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
