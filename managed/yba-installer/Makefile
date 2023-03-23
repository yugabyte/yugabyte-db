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
CRONTAB_SCRIPT_FOLDER:=$(CURDIR)/config/cron
CONFIG_INPUT_FILE_NAME:=$(CURDIR)/yba-ctl.yml.reference
VERSION_METADATA_JSON:=$(CURDIR)/version_metadata.json


#pre-reqs
PREREQS_DIR := ${TOP_BUILDDIR}/prereqs

# enterprisedb publishes statically built postgres for versions < 11
PG_BINARY_NAME ?= postgresql-10.23-1-linux-x64-binaries.tar.gz
PG_BINARY_URL := https://get.enterprisedb.com/postgresql/${PG_BINARY_NAME}
JDK_BINARY_NAME ?= OpenJDK8U-jdk_x64_linux_hotspot_8u345b01.tar.gz
JDK_BINARY_URL ?= https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u345-b01/OpenJDK8U-jdk_x64_linux_hotspot_8u345b01.tar.gz



all: prereqs test yba-ctl package

prereqs: postgres jdk

echo:
	@echo $(CURDIR)


postgres: ${PREREQS_DIR}/${PG_BINARY_NAME}
${PREREQS_DIR}/${PG_BINARY_NAME}:
	curl -Lo $@ ${PG_BINARY_URL}
	@echo downloaded $@


jdk: ${PREREQS_DIR}/${JDK_BINARY_NAME}
${PREREQS_DIR}/${JDK_BINARY_NAME}:
	curl -Lo $@ ${JDK_BINARY_URL}
	@echo downloaded $@



yba-ctl: test ${BIN_DIR}/${BIN_NAME}
${BIN_DIR}/${BIN_NAME}:
	CGO_ENABLED=0 GOOS=${GOOS} GOARCH=${GOARCH} go build -a -o $@ -tags ${BUILD_TAGS} -ldflags=" \
		-X 'components.ybactl.Version=${VERSION}' \
		-X 'components.ybactl.BuildID=${BUILD_ID}'"


package: ${PACKAGE_NAME}
${PACKAGE_NAME}: prereqs yba-ctl
	mkdir $(PACKAGE_DIR)
	cp -RL ${BIN_DIR}/${BIN_NAME} ${PREREQS_DIR}/* ${CONFIG_FILE_FOLDER} ${CRONTAB_SCRIPT_FOLDER} ${CONFIG_INPUT_FILE_NAME} ${VERSION_METADATA_JSON} ${VERSION_TXT} ${PACKAGE_DIR}
	tar -czvf ${PACKAGE_NAME} --remove-files ${PACKAGE_DIR}


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
test:
