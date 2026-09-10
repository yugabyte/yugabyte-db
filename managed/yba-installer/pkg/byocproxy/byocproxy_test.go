/*
 * Copyright (c) YugabyteDB, Inc.
 */

package byocproxy

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestIsValidVersion(t *testing.T) {
	valid := []string{"1.0.0", "0.0.1", "10.20.30"}
	for _, v := range valid {
		if !IsValidVersion(v) {
			t.Errorf("expected %q to be valid", v)
		}
	}
	invalid := []string{"", "latest", "1.0", "1.0.0.0", "1.0.0-SNAPSHOT", "v1.0.0", "1.0.0 "}
	for _, v := range invalid {
		if IsValidVersion(v) {
			t.Errorf("expected %q to be invalid", v)
		}
	}
}

func TestIsLatest(t *testing.T) {
	for _, v := range []string{"latest", "LATEST", " latest ", ""} {
		if !IsLatest(v) {
			t.Errorf("expected %q to select latest", v)
		}
	}
	for _, v := range []string{"1.0.0", "stable"} {
		if IsLatest(v) {
			t.Errorf("expected %q to not select latest", v)
		}
	}
}

func TestLessVersions(t *testing.T) {
	cases := []struct {
		v1, v2 string
		less   bool
	}{
		{"1.0.0", "1.0.1", true},
		{"1.0.1", "1.0.0", false},
		{"1.0.0", "1.0.0", false},
		{"1.9.0", "1.10.0", true},
		{"2.0.0", "10.0.0", true},
		{"1.2.3", "1.3.0", true},
	}
	for _, c := range cases {
		less, err := LessVersions(c.v1, c.v2)
		if err != nil {
			t.Errorf("LessVersions(%q, %q) unexpected error: %v", c.v1, c.v2, err)
			continue
		}
		if less != c.less {
			t.Errorf("LessVersions(%q, %q) = %v, want %v", c.v1, c.v2, less, c.less)
		}
	}
	if _, err := LessVersions("1.0", "1.0.0"); err == nil {
		t.Error("expected error for invalid version")
	}
}

func TestMaxVersion(t *testing.T) {
	cases := []struct {
		versions []string
		want     string
	}{
		{[]string{"1.0.0", "1.10.0", "1.9.9"}, "1.10.0"},
		{[]string{" 1.0.1 ", "1.0.0"}, "1.0.1"},
		{[]string{"garbage", "1.0.0-SNAPSHOT"}, ""},
		{[]string{}, ""},
		{[]string{"0.9.0", "not-a-version", "0.10.0"}, "0.10.0"},
	}
	for _, c := range cases {
		if got := MaxVersion(c.versions); got != c.want {
			t.Errorf("MaxVersion(%v) = %q, want %q", c.versions, got, c.want)
		}
	}
}

func TestResolveVersionPinned(t *testing.T) {
	got, err := ResolveVersion("http://unused.invalid", "1.2.3")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != "1.2.3" {
		t.Errorf("got %q, want 1.2.3", got)
	}

	if _, err := ResolveVersion("http://unused.invalid", "1.2.3-SNAPSHOT"); err == nil {
		t.Error("expected error for invalid pinned version")
	}
}

func listXML(truncated bool, token string, prefixes ...string) string {
	var sb strings.Builder
	sb.WriteString(`<?xml version="1.0" encoding="UTF-8"?>` + "\n")
	sb.WriteString(`<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">`)
	sb.WriteString(`<Name>downloads.yugabyte.com</Name><MaxKeys>1000</MaxKeys><Delimiter>/</Delimiter>`)
	fmt.Fprintf(&sb, "<IsTruncated>%t</IsTruncated>", truncated)
	if token != "" {
		fmt.Fprintf(&sb, "<NextContinuationToken>%s</NextContinuationToken>", token)
	}
	for _, p := range prefixes {
		fmt.Fprintf(&sb, "<CommonPrefixes><Prefix>%s</Prefix></CommonPrefixes>", p)
	}
	sb.WriteString(`</ListBucketResult>`)
	return sb.String()
}

func TestResolveVersionLatest(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		if r.URL.Path != "/byoc-api-proxy/" || q.Get("list-type") != "2" ||
			q.Get("prefix") != "byoc-api-proxy/" || q.Get("delimiter") != "/" {
			http.NotFound(w, r)
			return
		}
		// Folders that are not X.Y.Z versions must be ignored.
		w.Write([]byte(listXML(false, "",
			"byoc-api-proxy/1.0.0/", "byoc-api-proxy/1.2.0/", "byoc-api-proxy/1.1.5/",
			"byoc-api-proxy/docs/")))
	}))
	defer server.Close()

	got, err := ResolveVersion(server.URL+"/byoc-api-proxy", "latest")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != "1.2.0" {
		t.Errorf("got %q, want 1.2.0", got)
	}
}

func TestLatestRemoteVersionPaginated(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("continuation-token") == "page2" {
			w.Write([]byte(listXML(false, "", "1.2.0/")))
			return
		}
		w.Write([]byte(listXML(true, "page2", "1.0.0/", "1.1.0/")))
	}))
	defer server.Close()

	// Base URL with no path: versions sit at the bucket root, no prefix param.
	got, err := LatestRemoteVersion(server.URL)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != "1.2.0" {
		t.Errorf("got %q, want 1.2.0", got)
	}
}

func TestLatestRemoteVersionErrors(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/empty/":
			w.Write([]byte(listXML(false, "")))
		case "/html/":
			// A misrouted download site answers with an HTML page instead of the
			// S3 list API - the failure mode this feature replaced versions.txt for.
			w.Write([]byte("<html><body>Yugabyte DB Downloads</body></html>"))
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()

	for _, path := range []string{"/missing", "/empty", "/html"} {
		if _, err := LatestRemoteVersion(server.URL + path); err == nil {
			t.Errorf("expected error for %s", path)
		}
	}
}

func TestPackageURL(t *testing.T) {
	want := "https://downloads.yugabyte.com/byoc-api-proxy/1.0.0/byoc_api_proxy-1.0.0.tar.gz"
	got := PackageURL("https://downloads.yugabyte.com/byoc-api-proxy/", "1.0.0")
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestDownloadPackage(t *testing.T) {
	content := []byte("fake-tarball-bytes")
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/1.0.0/byoc_api_proxy-1.0.0.tar.gz" {
			w.Write(content)
			return
		}
		http.NotFound(w, r)
	}))
	defer server.Close()

	destDir := t.TempDir()
	path, err := DownloadPackage(server.URL, "1.0.0", destDir)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if path != filepath.Join(destDir, "byoc_api_proxy-1.0.0.tar.gz") {
		t.Errorf("unexpected download path %q", path)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("failed to read downloaded file: %v", err)
	}
	if string(data) != string(content) {
		t.Errorf("downloaded content mismatch")
	}

	if _, err := DownloadPackage(server.URL, "9.9.9", destDir); err == nil {
		t.Error("expected error for missing package")
	}
	entries, err := os.ReadDir(destDir)
	if err != nil {
		t.Fatalf("failed to list %s: %v", destDir, err)
	}
	if len(entries) != 1 {
		t.Errorf("expected only the successful download in %s, found %d entries", destDir, len(entries))
	}
}
