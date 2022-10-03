EXTENSION = hypopg
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
TESTS        = $(wildcard test/sql/*.sql)

# More test can be added later, after including pgxs
REGRESS      = hypopg

REGRESS_OPTS = --inputdir=test

PG_CONFIG ?= pg_config

MODULE_big = hypopg

OBJS = hypopg.o \
       hypopg_index.o \
       import/hypopg_import.o \
       import/hypopg_import_index.o

all:

release-zip: all
	git archive --format zip --prefix=hypopg-${EXTVERSION}/ --output ./hypopg-${EXTVERSION}.zip HEAD
	unzip ./hypopg-$(EXTVERSION).zip
	rm ./hypopg-$(EXTVERSION).zip
	rm ./hypopg-$(EXTVERSION)/.gitignore
	rm ./hypopg-$(EXTVERSION)/docs/ -rf
	rm ./hypopg-$(EXTVERSION)/typedefs.list
	rm ./hypopg-$(EXTVERSION)/TODO.md
	sed -i -e "s/__VERSION__/$(EXTVERSION)/g"  ./hypopg-$(EXTVERSION)/META.json
	zip -r ./hypopg-$(EXTVERSION).zip ./hypopg-$(EXTVERSION)/
	rm ./hypopg-$(EXTVERSION) -rf


DATA = $(wildcard *--*.sql)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

ifneq ($(MAJORVERSION),$(filter $(MAJORVERSION), 9.2 9.3 9.4))
	REGRESS += hypo_brin
endif

ifeq ($(MAJORVERSION),10)
	REGRESS += hypo_index_part_10
endif

ifneq ($(MAJORVERSION),$(filter $(MAJORVERSION), 9.2 9.3 9.4 9.5 9.6 10))
	REGRESS += hypo_index_part hypo_include
endif

ifneq ($(MAJORVERSION),$(filter $(MAJORVERSION), 9.2 9.3 9.4 9.5 9.6))
	REGRESS += hypo_hash
endif

DEBUILD_ROOT = /tmp/$(EXTENSION)

deb: release-zip
	mkdir -p $(DEBUILD_ROOT) && rm -rf $(DEBUILD_ROOT)/*
	unzip ./${EXTENSION}-$(EXTVERSION).zip -d $(DEBUILD_ROOT)
	cd $(DEBUILD_ROOT)/${EXTENSION}-$(EXTVERSION) && make -f debian/rules orig
	cd $(DEBUILD_ROOT)/${EXTENSION}-$(EXTVERSION) && debuild -us -uc -sa
