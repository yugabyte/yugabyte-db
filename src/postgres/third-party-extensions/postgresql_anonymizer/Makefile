##
## This Makefile is trying to mimic the targets and behaviour of the
## PGXS Makefile
##
## see https://github.com/postgres/postgres/blob/master/src/makefiles/pgxs.mk
##

PGRX?=cargo pgrx
PGVER?=$(shell grep 'default = \[".*\"]' Cargo.toml | sed -e 's/.*\["//' | sed -e 's/"].*//')
PG_MAJOR_VERSION=$(PGVER:pg%=%)
ANON_MINOR_VERSION?=$(shell grep '^version *= *' Cargo.toml | sed 's/^version *= *//' | tr -d '\"' | tr -d ' ' )

# use `TARGET=debug make run` for more detailed errors
TARGET?=release
TARGET_DIR?=target/$(TARGET)/anon-$(PGVER)/
# YB: PG_CONFIG comes from the shared pgrx fragment, included below.
#PG_CONFIG?=`$(PGRX) info pg-config $(PGVER) 2> /dev/null || echo pg_config`
PG_SHAREDIR?=$(shell $(PG_CONFIG) --sharedir)
PG_LIBDIR?=$(shell $(PG_CONFIG) --libdir)
PG_PKGLIBDIR?=$(shell $(PG_CONFIG) --pkglibdir)
PG_BINDIR?=$(shell $(PG_CONFIG) --bindir)

ifeq ($(shell uname -s),Darwin)
    LIB_SUFFIX?=dylib
else
    LIB_SUFFIX?=so
endif
LIB=anon.$(LIB_SUFFIX)

# The instance
PGDATA_DIR=~/.pgrx/data-$(PG_MAJOR_VERSION)

# Be sure to use the PGRX version (PGVER) of the postgres binaries
# It's especially important for the pg_dump test in pg_regress
PATH:=$(PG_BINDIR):${PATH}

# This is where the package is placed
TARGET_SHAREDIR?=$(TARGET_DIR)$(PG_SHAREDIR)
TARGET_PKGLIBDIR?=$(TARGET_DIR)$(PG_PKGLIBDIR)

PG_REGRESS?=$(PG_PKGLIBDIR)/pgxs/src/test/regress/pg_regress
PG_SOCKET_DIR?=/var/lib/postgresql/.pgrx/
PGHOST?=localhost
PGPORT?=288$(subst pg,,$(PGVER))
PSQL_OPT?=--host $(PGHOST) --port $(PGPORT)
PGDATABASE?=contrib_regression

##
## PGXS tests
##

# PGXS is used only for functional testing with `make installcheck`
# For unit tests, we use `cargo` via `make test`

# /!\ The test files should not have the same name that source files located
# in the `src` folder

REGRESS_TESTS = 000-setup
REGRESS_TESTS = initialize
REGRESS_TESTS+= anon_catalog
REGRESS_TESTS+= catalog_views
REGRESS_TESTS+= copy
REGRESS_TESTS+= test_custom_values
REGRESS_TESTS+= destruction
REGRESS_TESTS+= detection
REGRESS_TESTS+= drop_objects
REGRESS_TESTS+= dropped_columns
REGRESS_TESTS+= dummy
REGRESS_TESTS+= elevation_via_operator
REGRESS_TESTS+= faking
REGRESS_TESTS+= foreign_keys
REGRESS_TESTS+= fdw
REGRESS_TESTS+= generalization
REGRESS_TESTS+= generated_columns
REGRESS_TESTS+= get_function_schema
REGRESS_TESTS+= hashing
REGRESS_TESTS+= identity
REGRESS_TESTS+= image_blur
REGRESS_TESTs+= impexp
REGRESS_TESTS+= injection
REGRESS_TESTS+= k_anonymity
REGRESS_TESTS+= ldp
REGRESS_TESTS+= masking_cursors
REGRESS_TESTS+= masking_foreign_tables
REGRESS_TESTS+= masking_prepared_statements
REGRESS_TESTS+= multiple_masking_policies
REGRESS_TESTS+= noise
REGRESS_TESTS+= partial
REGRESS_TESTS+= permissions_masked_role
REGRESS_TESTS+= permissions_owner
REGRESS_TESTS+= pg_dump
REGRESS_TESTS+= privacy_by_default
REGRESS_TESTS+= pseudonymization
REGRESS_TESTS+= random_functions
REGRESS_TESTS+= restricted
REGRESS_TESTS+= rename_objects
#REGRESS_TESTS+= restore
REGRESS_TESTS+= rls
REGRESS_TESTS+= sampling
REGRESS_TESTS+= search_path
REGRESS_TESTS+= shuffle
REGRESS_TESTS+= syntax_checks
REGRESS_TESTS+= ternary
# The `test_` is here to avoid collision with the files in the `sql` folder
# DO NOT rename the `tests/sql/test_*` files !
REGRESS_TESTS+= test_replica_masking
REGRESS_TESTS+= test_static_masking
REGRESS_TESTS+= test_parallel_static_masking
REGRESS_TESTS+= transparent_dynamic_masking
REGRESS_TESTS+= trusted_schemas
REGRESS_TESTS+= views
REGRESS_TESTS+= when

# We try our best to write tests that produce the same output on all the 5
# current Postgres major versions. But sometimes it's really hard to do and
# we generally prefer simplicity over complex output manipulation tricks.
#
# In these few special cases, we use conditional tests with the following
# naming rules:
# * the _PG15+ suffix means PostgreSQL 15 and all the major versions after
# * the _PG13- suffix means PostgreSQL 13 and all the major versions below

REGRESS_TESTS_PG14 = elevation_via_security_definer_function_PG14-
REGRESS_TESTS_PG15 =
REGRESS_TESTS_PG16 =
REGRESS_TESTS_PG17 =

REGRESS_TESTS+=${REGRESS_TESTS_PG${PG_MAJOR_VERSION}}

# Use this var to add more tests
#PG_TEST_EXTRA ?= ""
REGRESS_TESTS+=$(PG_TEST_EXTRA)

# This can be overridden by an env variable
REGRESS?=$(REGRESS_TESTS)


EXTRA_CLEAN?=target


##
## BUILD
##

# YB: Toolchain detection, cargo env scrubbing, and PG_CONFIG/CARGO_BIN live in
# the shared pgrx fragment. cargo-pgrx itself is built by the pgrx directory,
# ordered ahead of this directory by the parent Makefile. The $(YB_CARGO_PGRX) rule below
# bootstraps it for direct invocations only.

include ../pgrx/pgrx-common.mk

# YB: `all` stays the first (default) target so `make` is a no-op build; the real
# work happens in `install`.
all:

# YB: Direct invocations bootstrap cargo-pgrx themselves. Under the parent
# Makefile install-pgrx has already built it, so this file rule does not fire.
$(YB_CARGO_PGRX):
	$(MAKE) -C $(YB_PGRX_DIR) cargo-pgrx


##
## INSTALL
##

# YB: install the extension with the shared cargo-pgrx, then copy the anonymizer
# data CSVs, which cargo-pgrx does not handle.
install: $(YB_CARGO_PGRX)
	export PATH="$(CARGO_DIR):$$PATH" && \
	$(YB_PGRX_CARGO_ENV) \
		$(YB_PGRX_RUN_ENV) \
		$(YB_CARGO_PGRX) pgrx install --release -F pg15 -c $(PG_CONFIG)
	mkdir -p $(PG_SHAREDIR)/extension/anon/
	install data/*.csv $(PG_SHAREDIR)/extension/anon/
	install data/en_US/fake/*.csv $(PG_SHAREDIR)/extension/anon/

##
## INSTALLCHECK
##
## These are the functional tests, the unit tests are run with Cargo
##

# With PGXS: the postgres instance is created on-the-fly to run the test.
# With PGRX: the postgres instance is created previously by `cargo run`. This
# means we have some extra tasks to prepare the instance

installcheck: stop start
	dropdb $(PSQL_OPT) --if-exists $(PGDATABASE)
	dropdb $(PSQL_OPT) --if-exists $(PGDATABASE)_source
	createdb $(PSQL_OPT) $(PGDATABASE)
	dropuser oscar_the_owner || echo 'ignored'
	createuser $(PSQL_OPT) postgres --superuser || echo 'ignored'
	psql $(PSQL_OPT) $(PGDATABASE) -c "ALTER DATABASE $(PGDATABASE) SET session_preload_libraries = 'anon';"
	psql $(PSQL_OPT) $(PGDATABASE) -c "ALTER DATABASE $(PGDATABASE) SET anon.masking_policies = 'devtests, analytics, impexp';"
	psql $(PSQL_OPT) $(PGDATABASE) -c "ALTER DATABASE $(PGDATABASE) SET client_min_messages = notice;"
	$(PG_REGRESS) \
		$(PSQL_OPT) \
		--use-existing \
		--inputdir=./tests/ \
		--dbname=$(PGDATABASE) \
		$(REGRESS_OPTS) \
		$(REGRESS)


##
## PGRX commands
##

ifeq ($(TARGET),release)
  RELEASE_OPT=--release
endif

test:
	$(PGRX) test $(PGVER) $(RELEASE_OPT) --verbose

start:
	sed --in-place 's/^#\?wal_level = .*/wal_level = logical/' $(PGDATA_DIR)/postgresql.conf
	$(PGRX) start $(PGVER)

stop:
	$(PGRX) stop $(PGVER)

run:
	# ensure that the wal_level is properly set
	sed --in-place 's/^#\?wal_level = .*/wal_level = logical/' $(PGDATA_DIR)/postgresql.conf
	$(PGRX) run $(PGVER) $(RELEASE_OPT)

psql:
	psql --host localhost --port 288$(PG_MAJOR_VERSION) anon

##
## Coverage
##

COVERAGE_DIR?=target/$(TARGET)/coverage

clean_profiles:
	rm -fr *.profraw

coverage: clean_profiles coverage_test covergage_report

coverage_test:
	export RUSTFLAGS=-Cinstrument-coverage \
	export LLVM_PROFILE_FILE=$(TARGET)-%p-%m.profraw \
	&& $(PGRX) test $(PGVER) $(RELEASE_OPT) --verbose

coverage_report:
	mkdir -p $(COVERAGE_DIR)
	export LLVM_PROFILE_FILE=$(TARGET)-%p-%m.profraw \
	&& grcov . \
	      --binary-path target/$(TARGET) \
	      --source-dir . \
	      --output-path $(COVERAGE_DIR)\
	      --keep-only 'src/*' \
	      --llvm \
	      --ignore-not-existing \
	      --output-types html,cobertura
	# Terse output
	grep '<p class="heading">Lines</p>' -A2 $(COVERAGE_DIR)/html/index.html \
	  | tail -n 1 \
	  | xargs \
	  | sed 's,%.*,,' \
	  | sed 's/.*>/Coverage: /'

##
## C L E A N
##

clean: clean_profiles
ifdef EXTRA_CLEAN
	rm -rf $(EXTRA_CLEAN)
endif


##
## All targets below are not part of the PGXS Makefile
##

##
## P A C K A G E S
##

# See https://nfpm.goreleaser.com/docs/arch-mapping/
ARCH?=amd64

# The packages are built from the $(TARGET_DIR) folder.
# So the $(PG_PKGLIBDIR) and $(PG_SHAREDIR) are relative to that folder
rpm deb: package
	export PG_PKGLIBDIR=".$(PG_PKGLIBDIR)" && \
	export PG_SHAREDIR=".$(PG_SHAREDIR)" && \
	export PG_MAJOR_VERSION="$(PG_MAJOR_VERSION)" && \
	export ANON_MINOR_VERSION="$(ANON_MINOR_VERSION)" && \
	envsubst < nfpm.template.yaml > $(TARGET_DIR)/nfpm.yaml
	cd $(TARGET_DIR) && nfpm package --packager $@


# The `package` command needs pg_config from the target version
# https://github.com/pgcentralfoundation/pgrx/issues/288

package:
	$(PGRX) package --pg-config $(PG_CONFIG)

##
## O C I
##

OCI_DISTRO?=bookworm
OCI_TAG?=$(PG_MAJOR_VERSION)-$(ANON_MINOR_VERSION)-$(OCI_DISTRO)
OCI_IMAGE?=registry.gitlab.com/dalibo/postgresql_anonymizer:$(OCI_TAG)
OCI_ARCH?=amd64

DOCKER_BUILD_ARG := \
  --build-arg PG_MAJOR_VERSION=$(PG_MAJOR_VERSION) \
  --build-arg OCI_DISTRO=$(OCI_DISTRO)

oci_image: docker/oci/Dockerfile #: build the OCI image
	docker build $(DOCKER_BUILD_ARG) --tag $(OCI_IMAGE) --file $^ .

oci_push: #: push the docker image to the registry
	docker push $(OCI_IMAGE)

##
## D O C K E R
##

DOCKER_TAG?=latest
DOCKER_IMAGE?=registry.gitlab.com/dalibo/postgresql_anonymizer:$(DOCKER_TAG)

DOCKER_BUILD_ARG := --build-arg ANON_MINOR_VERSION=${ANON_MINOR_VERSION}

ifneq ($(DOCKER_PG_MAJOR_VERSION),)
DOCKER_BUILD_ARG += --build-arg DOCKER_PG_MAJOR_VERSION=$(DOCKER_PG_MAJOR_VERSION)
endif

PGRX_VERSION?=0.17.0
PGRX_TAG?=pgrx-$(PGRX_VERSION)
PGRX_IMAGE?=registry.gitlab.com/dalibo/postgresql_anonymizer:$(PGRX_TAG)
PGRX_BUILD_ARGS:=--build-arg PGRX_VERSION=$(PGRX_VERSION)

docker_image: docker/Dockerfile #: build the docker image
	docker build --tag $(DOCKER_IMAGE) . --file $^  $(DOCKER_BUILD_ARG)

pgrx_image: docker/pgrx/Dockerfile
	docker build --tag $(PGRX_IMAGE) . --file $^ $(PGRX_BUILD_ARGS)

docker_push: #: push the docker image to the registry
	docker push $(DOCKER_IMAGE)

pgrx_push:
	docker push $(PGRX_IMAGE)

docker_bash: #: enter the docker image (useful for testing)
	docker exec -it docker-PostgreSQL-1 bash

pgrx_bash:
	docker run --rm --interactive --tty --volume  `pwd`:/pgrx $(PGRX_IMAGE)

COMPOSE=docker compose --file docker/docker-compose.yml

docker_init: #: start a docker container
	$(COMPOSE) down
	$(COMPOSE) up -d
	@echo "The Postgres server may take a few seconds to start. Please wait."

##
## L I N T
##


lint: lint-rs lint-md

lint-md:
	uvx rumdl check

lint-rs:
	cargo clippy --release -- -Dwarnings

##
## D O C S
##

MKDOCS?=uvx mkdocs

.PHONY: docs
docs:
	$(MKDOCS) build

##
## DALIBO-specific Makefile
##
-include dalibo/Makefile
