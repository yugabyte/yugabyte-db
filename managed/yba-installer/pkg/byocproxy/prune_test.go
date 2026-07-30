/*
 * Copyright (c) YugabyteDB, Inc.
 */

package byocproxy

import (
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"testing"
)

func TestPruneOldReleases(t *testing.T) {
	baseDir := t.TempDir()
	downloadDir := filepath.Join(baseDir, "downloads")

	mkVersionDir := func(version string) {
		dir := filepath.Join(baseDir, version, "bin")
		if err := os.MkdirAll(dir, 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "byoc-api-proxy.jar"), []byte("jar"),
			0644); err != nil {
			t.Fatal(err)
		}
	}
	mkPackage := func(name string) {
		if err := os.MkdirAll(downloadDir, 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(downloadDir, name), []byte("pkg"), 0644); err != nil {
			t.Fatal(err)
		}
	}

	for _, v := range []string{"1.0.0", "1.1.0", "1.2.0"} {
		mkVersionDir(v)
		mkPackage(PackageName(v))
	}
	// Manually dropped package for a future upgrade (airgapped host): no
	// extracted directory, must survive.
	mkPackage(PackageName("9.9.9"))
	// Leftover from an interrupted download.
	mkPackage(PackageName("1.2.0") + ".download-12345")
	if err := os.Symlink(filepath.Join(baseDir, "1.2.0"), filepath.Join(baseDir,
		"active")); err != nil {
		t.Fatal(err)
	}

	removed, err := PruneOldReleases(baseDir, downloadDir, "1.2.0")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	wantRemoved := []string{
		filepath.Join(baseDir, "1.0.0"),
		filepath.Join(downloadDir, PackageName("1.0.0")),
		filepath.Join(downloadDir, PackageName("1.2.0")+".download-12345"),
	}
	sort.Strings(removed)
	sort.Strings(wantRemoved)
	if !reflect.DeepEqual(removed, wantRemoved) {
		t.Errorf("removed %v, want %v", removed, wantRemoved)
	}

	for _, path := range wantRemoved {
		if _, err := os.Lstat(path); !os.IsNotExist(err) {
			t.Errorf("expected %s to be removed", path)
		}
	}
	for _, path := range []string{
		filepath.Join(baseDir, "1.1.0"),
		filepath.Join(baseDir, "1.2.0", "bin", "byoc-api-proxy.jar"),
		filepath.Join(baseDir, "active"),
		filepath.Join(downloadDir, PackageName("1.1.0")),
		filepath.Join(downloadDir, PackageName("1.2.0")),
		filepath.Join(downloadDir, PackageName("9.9.9")),
	} {
		if _, err := os.Lstat(path); err != nil {
			t.Errorf("expected %s to survive prune: %v", path, err)
		}
	}

	// A second prune is a no-op: active plus one rollback version remain.
	removed, err = PruneOldReleases(baseDir, downloadDir, "1.2.0")
	if err != nil {
		t.Fatalf("unexpected error on re-prune: %v", err)
	}
	if len(removed) != 0 {
		t.Errorf("expected nothing removed on re-prune, got %v", removed)
	}
}

func TestPruneOldReleasesInvalidActive(t *testing.T) {
	for _, active := range []string{"", "latest"} {
		if _, err := PruneOldReleases(t.TempDir(), t.TempDir(), active); err == nil {
			t.Errorf("expected error for active version %q", active)
		}
	}
}

func TestPruneOldReleasesMissingDirs(t *testing.T) {
	baseDir := filepath.Join(t.TempDir(), "does-not-exist")
	removed, err := PruneOldReleases(baseDir, filepath.Join(baseDir, "downloads"), "1.0.0")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(removed) != 0 {
		t.Errorf("expected nothing removed, got %v", removed)
	}
}
