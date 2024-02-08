TOP_BUILDDIR ?= .
DEVOPS_HOME=$(CURDIR)/../devops


VERSION ?= $(shell cat $(CURDIR)/version.txt)
BUILD_ID ?= $(shell cat $(CURDIR)/version_metadata.json | jq '.build_id')

# go compile options
GOOS ?= linux
GOARCH ?= amd64

# Build Tags
BUILD_TYPE := preview

BUILD_TAGS = ${BUILD_TYPE}

# build requirements
BIN_NAME ?= yba

BIN_DIR := ${TOP_BUILDDIR}/bin
PACKAGE_DIR := ${TOP_BUILDDIR}/yba_cli-${VERSION}-${GOOS}-${GOARCH}
PACKAGE_NAME ?= ${PACKAGE_DIR}.tar.gz

VERSION_METADATA_JSON:=$(CURDIR)/version_metadata.json
VERSION_TXT:=$(CURDIR)/version.txt


all: yba package

echo:
	@echo $(CURDIR)

yba: test ${BIN_DIR}/${BIN_NAME}
${BIN_DIR}/${BIN_NAME}:
	CGO_ENABLED=0 GOOS=${GOOS} GOARCH=${GOARCH} go build -a -o $@ -tags ${BUILD_TAGS} -ldflags=" \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-cli/pkg/components/yba.Version=${VERSION}' \
	-X 'github.com/yugabyte/yugabyte-db/managed/yba-cli/pkg/components/yba.BuildID=${BUILD_ID}'"


package: ${PACKAGE_NAME}
${PACKAGE_NAME}: yba
	mkdir $(PACKAGE_DIR)
	cp -RL ${BIN_DIR}/${BIN_NAME} ${VERSION_METADATA_JSON} ${VERSION_TXT} ${PACKAGE_DIR}
	tar -czvf ${PACKAGE_NAME} ${PACKAGE_DIR} && rm -R ${PACKAGE_DIR}



.PHONY: test
test:

.PHONY: clean clean-bin clean-package
clean: clean-bin clean-package
clean-bin:
	rm -rf bin/*

clean-package:
	rm -rf ${PACKAGE_DIR}
	rm -rf ${PACKAGE_NAME}

.PHONY: updateclient
updateclient:
	go get github.com/yugabyte/platform-go-client

.PHONY: build
build:
	go build -ldflags="-X 'main.version=v${VERSION}'" -o ${BIN_NAME}

.PHONY: doc
doc:
	YBA_FF_TOOLS=true  go run main.go tools gen-doc --format markdown
