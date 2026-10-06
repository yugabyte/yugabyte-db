TOP_BUILDDIR ?= .
DEVOPS_HOME=$(CURDIR)/../devops


VERSION ?= $(shell cat $(CURDIR)/version.txt)
BUILD_ID ?= $(shell cat $(CURDIR)/version_metadata.json | jq '.build_id')

# go compile options
GOOS ?= linux
GOARCH ?= amd64

# Build Tags
BUILD_TYPE := preview
USE_LICENSE := true

BUILD_TAGS = ${BUILD_TYPE}
ifeq ($(USE_LICENSE), false)
	BUILD_TAGS := ${BUILD_TAGS},nolicense
endif

# build requirements
BIN_NAME ?= yba-ctl
LICENSE_GEN := licensectl

BIN_DIR := ${TOP_BUILDDIR}/bin
PACKAGE_DIR := ${TOP_BUILDDIR}/yba_installer-${VERSION}-${GOOS}-${GOARCH}
PACKAGE_NAME ?= ${PACKAGE_DIR}.tar.gz

CONFIG_FILE_FOLDER:=$(CURDIR)/config/templates
CONFIG_INPUT_FILE_NAME:=$(CURDIR)/yba-ctl.yml.reference
CONFIG_EMBEDED_NAME:=$(CURDIR)/pkg/config/yba-ctl.yml
VERSION_METADATA_JSON:=$(CURDIR)/version_metadata.json

# get the perf advisor version to bundle
PERF_ADVISOR_VERSION ?= 1.4.0-b10
PA_BINARY_NAME ?= perf_advisor-$(PERF_ADVISOR_VERSION).tar.gz
PA_BINARY_URL := https://s3.us-west-2.amazonaws.com/releases.yugabyte.com/perf-advisor/$(PERF_ADVISOR_VERSION)/$(PA_BINARY_NAME)
PA_BINARY_SHA_URL := $(PA_BINARY_URL).sha

# Prefer coreutils' sha1sum (Linux); fall back to `shasum -a 1` (macOS).
SHA1SUM ?= $(shell command -v sha1sum >/dev/null 2>&1 && echo sha1sum || echo shasum -a 1)

#pre-reqs
PREREQS_DIR := ${TOP_BUILDDIR}/prereqs

# Prometheus Version
PROMETHEUS_VERSION ?= 3.13.1

# Node Exporter Version
NODE_EXPORTER_VERSION ?= 1.10.2

# https://github.com/yugabyte/yba-pg-build/releases/tag/2025-03-05-17-09
POSTGRES_VERSION ?= 14.23
PG_BINARY_NAME ?= postgres-linux-${POSTGRES_VERSION}.tar.gz
PG_BINARY_URL := https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/yba-pg-build/${PG_BINARY_NAME}

# https://adoptium.net/temurin/releases/
JDK_BINARY_NAME ?= OpenJDK17U-jre_x64_linux_hotspot_17.0.7_7.tar.gz
JDK_BINARY_URL ?= https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.7%2B7/OpenJDK17U-jre_x64_linux_hotspot_17.0.7_7.tar.gz
# Package stable ybdb release in yba-installer.
# Currently disabled.
YBDB_BINARY_NAME ?= yugabyte-2.17.2.0-b216-linux-x86_64.tar.gz
YBDB_BINARY_URL ?= https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/releases/2.17.2.0/yugabyte-2.17.2.0-b216-linux-x86_64.tar.gz

# Perf Advisor collects table DDL with ysql_dump, which nothing else on a yba-installer host
# provides. We repackage just ysql_dump and the YugabyteDB C++ runtime it links against out of a
# YBDB release, rather than shipping the whole release.
YSQL_DUMP_YBDB_VERSION ?= 2026.1.0.0-b118
YSQL_DUMP_YBDB_RELEASE ?= $(shell echo $(YSQL_DUMP_YBDB_VERSION) | sed 's/-b.*//')
YSQL_DUMP_YBDB_PLATFORM ?= $(if $(filter arm64,$(GOARCH)),el8-aarch64,linux-x86_64)
YSQL_DUMP_YBDB_BINARY_NAME ?= yugabyte-$(YSQL_DUMP_YBDB_VERSION)-$(YSQL_DUMP_YBDB_PLATFORM).tar.gz
YSQL_DUMP_YBDB_BINARY_URL ?= \
  https://downloads.yugabyte.com/releases/$(YSQL_DUMP_YBDB_RELEASE)/$(YSQL_DUMP_YBDB_BINARY_NAME)
YSQL_DUMP_CLIENT_BUNDLE ?= ysql_dump_client.tar.gz
YSQL_DUMP_HOST_ARCH := $(if $(filter arm64,$(GOARCH)),aarch64,x86_64)

# Libraries ysql_dump needs at runtime; the release moves them between directories across
# versions, so the recipe below probes for the directory that has all of them.
YSQL_DUMP_REQUIRED_LIBS := 'libunwind\.so(\.[0-9]+)*$$' 'libc\+\+\.so(\.[0-9]+)*$$' \
  'libc\+\+abi\.so(\.[0-9]+)*$$' 'libabsl[^/]*\.so(\.[0-9]+)*$$'



# Remove partially built targets on recipe failure so the next build re-runs
# the recipe from scratch instead of picking up a stale/truncated file.
.DELETE_ON_ERROR:

all: test package

prereqs: postgres jdk perf_advisor ysql_dump_client

echo:
	@echo $(CURDIR)

perf_advisor: ${PREREQS_DIR}/${PA_BINARY_NAME}
${PREREQS_DIR}/${PA_BINARY_NAME}:
	curl -fLo $@ ${PA_BINARY_URL}
	@echo downloaded $@
	@echo "attempting to download SHA1 checksum from ${PA_BINARY_SHA_URL}"
	@if curl -fLo $@.sha ${PA_BINARY_SHA_URL}; then \
	    actual=$$($(SHA1SUM) $@ | awk '{print $$1}'); \
	    expected=$$(awk '{print $$1}' $@.sha); \
	    rm -f $@.sha; \
	    if [ -z "$$expected" ]; then \
	        echo "ERROR: could not parse SHA1 from $(PA_BINARY_NAME).sha"; \
	        exit 1; \
	    fi; \
	    if [ "$$actual" != "$$expected" ]; then \
	        echo "ERROR: SHA1 mismatch for $(PA_BINARY_NAME): expected=$$expected actual=$$actual"; \
	        exit 1; \
	    fi; \
	    echo "SHA1 verified for $(PA_BINARY_NAME) ($$expected)"; \
	else \
	    rm -f $@.sha; \
	    echo "ERROR: could not download ${PA_BINARY_SHA_URL}"; \
	    exit 1; \
	fi

postgres: ${PREREQS_DIR}/${PG_BINARY_NAME}
${PREREQS_DIR}/${PG_BINARY_NAME}:
	curl -fLo $@ ${PG_BINARY_URL}
	@echo downloaded $@


jdk: ${PREREQS_DIR}/${JDK_BINARY_NAME}
${PREREQS_DIR}/${JDK_BINARY_NAME}:
	curl -fLo $@ ${JDK_BINARY_URL}
	@echo downloaded $@

ybdb: ${PREREQS_DIR}/${YBDB_BINARY_NAME}
${PREREQS_DIR}/${YBDB_BINARY_NAME}:
	curl -fLo $@ ${YBDB_BINARY_URL}
	@echo downloaded $@

# Bundles ysql_dump with the YugabyteDB runtime libraries it links against, out of a full YBDB
# release. Two things are dropped, together ~90% of the release: the rest of the postgres tree
# (server binaries, headers, share), and tserver/lib/yb, which holds the YugabyteDB server
# libraries no client links against. The recipe then runs the bundled binary against only the
# libraries the bundle ships, which is what proves the trim is complete; that check needs a host
# that can execute the target's binaries, so it is skipped when cross-building.
ysql_dump_client: ${PREREQS_DIR}/${YSQL_DUMP_CLIENT_BUNDLE}
${PREREQS_DIR}/${YSQL_DUMP_CLIENT_BUNDLE}:
	set -eu; \
	tmp_dir=$$(mktemp -d); \
	trap 'rm -rf "$$tmp_dir"' EXIT; \
	expanded_dir="$$tmp_dir/expanded"; \
	bundle_dir="$$tmp_dir/bundle"; \
	mkdir -p "$$expanded_dir" "$$bundle_dir/postgres/bin" "$$bundle_dir/tserver"; \
	curl -fLo "$$tmp_dir/ybdb.tar.gz" "$(YSQL_DUMP_YBDB_BINARY_URL)"; \
	tar -zxf "$$tmp_dir/ybdb.tar.gz" -C "$$expanded_dir" --strip-components=1; \
	if [ ! -x "$$expanded_dir/postgres/bin/ysql_dump" ]; then \
	    echo "ERROR: postgres/bin/ysql_dump missing from $(YSQL_DUMP_YBDB_BINARY_NAME)"; \
	    exit 1; \
	fi; \
	cp -a "$$expanded_dir/postgres/bin/ysql_dump" "$$bundle_dir/postgres/bin/"; \
	lib_dir=""; \
	for candidate in "$$expanded_dir/tserver/lib" "$$expanded_dir/lib" \
	                 "$$expanded_dir/thirdparty"; do \
	    [ -d "$$candidate" ] || continue; \
	    files=$$(find "$$candidate" -type f); \
	    complete=1; \
	    for lib in $(YSQL_DUMP_REQUIRED_LIBS); do \
	        printf '%s\n' "$$files" | grep -Eq "$$lib" || complete=0; \
	    done; \
	    if [ "$$complete" -eq 1 ]; then lib_dir="$$candidate"; break; fi; \
	    echo "skipping $$candidate: missing some of ysql_dump's runtime libraries"; \
	done; \
	if [ -z "$$lib_dir" ]; then \
	    echo "ERROR: no runtime library directory in $(YSQL_DUMP_YBDB_BINARY_NAME) has every"; \
	    echo "       library ysql_dump needs (checked tserver/lib, lib, thirdparty)"; \
	    exit 1; \
	fi; \
	echo "using runtime libraries from $$lib_dir"; \
	cp -a "$$lib_dir" "$$bundle_dir/tserver/lib"; \
	rm -rf "$$bundle_dir/tserver/lib/yb"; \
	if [ "$$(uname -s)" = "Linux" ] && [ "$(GOOS)" = "linux" ] && \
	   [ "$$(uname -m)" = "$(YSQL_DUMP_HOST_ARCH)" ]; then \
	    libs="$$bundle_dir/tserver/lib"; \
	    LD_LIBRARY_PATH="$$libs/yb-thirdparty:$$libs/postgres:$$libs" \
	        "$$bundle_dir/postgres/bin/ysql_dump" --version; \
	else \
	    echo "skipping ysql_dump smoke test: cannot run $(GOOS)/$(GOARCH) binaries here"; \
	fi; \
	tar -czf $@ -C "$$bundle_dir" postgres tserver/lib
	@echo packaged $@

yba-ctl: config test ${BIN_DIR}/${BIN_NAME}
${BIN_DIR}/${BIN_NAME}:
	CGO_ENABLED=0 GOOS=${GOOS} GOARCH=${GOARCH} go build -a -o $@ -tags ${BUILD_TAGS} -ldflags=" \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl.Version=${VERSION}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl.BuildID=${BUILD_ID}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd.PostgresVersion=${POSTGRES_VERSION}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd.PrometheusVersion=${PROMETHEUS_VERSION}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd.PerfAdvisorVersion=${PERF_ADVISOR_VERSION}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-installer/cmd.NodeExporterVersion=${NODE_EXPORTER_VERSION}'"


package: ${PACKAGE_NAME}
${PACKAGE_NAME}: prereqs yba-ctl
	mkdir -p $(PACKAGE_DIR)
	cp -RL ${BIN_DIR}/${BIN_NAME} ${PREREQS_DIR}/* ${CONFIG_FILE_FOLDER} ${CONFIG_INPUT_FILE_NAME} ${VERSION_METADATA_JSON} ${VERSION_TXT} ${PACKAGE_DIR}
	tar -czvf ${PACKAGE_NAME} ${PACKAGE_DIR} && rm -R ${PACKAGE_DIR}


licensectl: test ${BIN_DIR}/${LICENSE_GEN}
${BIN_DIR}/${LICENSE_GEN}:
	cd licensectl && \
	  CGO_ENABLED=0 GOOS=${GOOS} GOARCH=${GOARCH} go build -a -o ../$@

.PHONY: clean clean-bin clean-prereqs clean-package
clean: clean-bin clean-prereqs clean-package
clean-bin:
	rm -rf bin/*
clean-prereqs:
	rm -rf prereqs/*
clean-package:
	rm -rf ${PACKAGE_DIR}
	rm -rf ${PACKAGE_NAME}

.PHONY: test
test: config
	go test ./...

.PHONY: config
config:
	cp ${CONFIG_INPUT_FILE_NAME} ${CONFIG_EMBEDED_NAME}

.PHONY: integrationtest
integrationtest: config
	go test -tags integration ./integrationtests/...
