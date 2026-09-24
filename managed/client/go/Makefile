default: testv1 testv2

GOIMPORTS_VERSION := v0.30.0
GOIMPORTS := $(shell go env GOPATH)/bin/goimports

testv1: buildv1
	cd v1; go test ./...

buildv1: fmtv1
	cd v1; go build ./...

fmtv1: gogetv1 $(GOIMPORTS)
	cd v1; $(GOIMPORTS) -w .

gogetv1:
	cd v1; go get ./...

testv2: buildv2
	cd v2; go test ./...

buildv2: fmtv2
	cd v2; go build ./...

fmtv2: gogetv2 $(GOIMPORTS)
	cd v2; $(GOIMPORTS) -w .

gogetv2:
	cd v2; go get ./...

$(GOIMPORTS):
	go install golang.org/x/tools/cmd/goimports@$(GOIMPORTS_VERSION)
