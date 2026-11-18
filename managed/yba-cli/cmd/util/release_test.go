/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"encoding/json"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
)

func TestKeepReleaseArtifactsSendsOnlyPlatformAndArchitecture(t *testing.T) {
	artifacts := []ybaclient.Artifact{
		{Platform: "LINUX", Architecture: "x86_64", PackageUrl: "https://example.com/yb.tar.gz"},
		{Platform: "LINUX", Architecture: "aarch64", PackageFileId: "file-uuid"},
		{Platform: "KUBERNETES", PackageUrl: "https://example.com/yb.tgz"},
	}
	got, err := json.Marshal(KeepReleaseArtifacts(artifacts))
	if err != nil {
		t.Fatal(err)
	}
	want := `[{"platform":"LINUX","architecture":"x86_64"},` +
		`{"platform":"LINUX","architecture":"aarch64"},` +
		`{"platform":"KUBERNETES"}]`
	if string(got) != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestKeepReleaseArtifactsNoArtifacts(t *testing.T) {
	got, err := json.Marshal(UpdateReleaseRequest{Artifacts: KeepReleaseArtifacts(nil)})
	if err != nil {
		t.Fatal(err)
	}
	want := `{"artifacts":[],"release_date":0,"release_notes":"","release_tag":"","state":""}`
	if string(got) != want {
		t.Errorf("got %s, want %s", got, want)
	}
}
