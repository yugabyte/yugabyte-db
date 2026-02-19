default: testv1 testv2

testv1: buildv1
	cd v1; go test ./...

buildv1: fmtv1
	cd v1; go build ./...

fmtv1: gogetv1
	cd v1; goimports -w .

gogetv1:
	cd v1; go get ./...

testv2: buildv2
	cd v2; go test ./...

buildv2: fmtv2
	cd v2; go build ./...

fmtv2: gogetv2
	cd v2; goimports -w .

gogetv2:
	cd v2; go get ./...