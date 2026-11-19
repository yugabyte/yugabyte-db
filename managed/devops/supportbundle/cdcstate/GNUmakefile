GOOS ?= linux
GOARCH ?= amd64
BIN_DIR := ../../bin
PACKAGE := yb-dump-cdc-state

.PHONY: all test build build-all clean

all: test build-all

test:
	go test ./...

# Host-arch build, for local iteration. Emits the same arch-suffixed name as a release
# build so that YBA can resolve it from devops/bin.
build: build-platform

# Release artifacts for every supported DB node platform. Deliberately not gated on
# `test`: devops/yb_release calls this target directly, and packaging must not depend
# on the test suite. `make all` runs tests.
build-all:
	$(MAKE) build-platform GOOS=linux GOARCH=amd64
	$(MAKE) build-platform GOOS=linux GOARCH=arm64

.PHONY: build-platform
build-platform:
	CGO_ENABLED=0 GOOS=$(GOOS) GOARCH=$(GOARCH) go build -trimpath -ldflags="-s -w" \
		-o $(BIN_DIR)/$(PACKAGE)-$(GOOS)-$(GOARCH) ./cmd/yb-dump-cdc-state

clean:
	rm -f $(BIN_DIR)/$(PACKAGE) $(BIN_DIR)/$(PACKAGE)-linux-amd64 $(BIN_DIR)/$(PACKAGE)-linux-arm64
