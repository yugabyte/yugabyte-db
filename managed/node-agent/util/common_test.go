/*
 * Copyright (c) YugabyteDB, Inc.
 */
package util

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestRemoveSubfolders(t *testing.T) {
	root := t.TempDir()
	keepFile := filepath.Join(root, "keep.txt")
	if err := os.WriteFile(keepFile, []byte("keep"), 0644); err != nil {
		t.Fatalf("Unable to write keep file - %s", err.Error())
	}
	sub1 := filepath.Join(root, "sub1")
	sub2 := filepath.Join(root, "sub2")
	if err := os.MkdirAll(filepath.Join(sub1, "nested"), 0755); err != nil {
		t.Fatalf("Unable to create sub1 - %s", err.Error())
	}
	if err := os.MkdirAll(sub2, 0755); err != nil {
		t.Fatalf("Unable to create sub2 - %s", err.Error())
	}
	if err := os.WriteFile(filepath.Join(sub1, "nested", "f.txt"), []byte("x"), 0644); err != nil {
		t.Fatalf("Unable to write nested file - %s", err.Error())
	}

	if err := RemoveSubfolders(root); err != nil {
		t.Fatalf("RemoveSubfolders failed - %s", err.Error())
	}

	if _, err := os.Stat(keepFile); err != nil {
		t.Fatalf("Expected non-directory file to remain - %s", err.Error())
	}
	if _, err := os.Stat(sub1); !os.IsNotExist(err) {
		t.Fatalf("Expected sub1 to be removed, got err=%v", err)
	}
	if _, err := os.Stat(sub2); !os.IsNotExist(err) {
		t.Fatalf("Expected sub2 to be removed, got err=%v", err)
	}
}

func TestRemoveSubfoldersEmptyDir(t *testing.T) {
	root := t.TempDir()
	if err := RemoveSubfolders(root); err != nil {
		t.Fatalf("RemoveSubfolders on empty dir failed - %s", err.Error())
	}
}

func TestRemoveSubfoldersMissingDir(t *testing.T) {
	err := RemoveSubfolders(filepath.Join(t.TempDir(), "does-not-exist"))
	if err == nil {
		t.Fatal("Expected error for missing directory")
	}
}

func TestPlatformGetCertificateEndpoint(t *testing.T) {
	endpoint := PlatformGetCertificateEndpoint("c1234", "my cert/name")
	expected := "/api/customers/c1234/certificates/my%20cert%2Fname"
	if endpoint != expected {
		t.Fatalf("Expected %q, got %q", expected, endpoint)
	}
}

func TestWriteFileIfChanged(t *testing.T) {
	path := filepath.Join(t.TempDir(), "web.yml")
	if err := WriteFileIfChanged(path, "a: 1\n", 0640, ""); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0640 {
		t.Errorf("mode = %v, want 0640", info.Mode().Perm())
	}
	// Unchanged content leaves the file alone.
	before := info.ModTime()
	time.Sleep(10 * time.Millisecond)
	if err := WriteFileIfChanged(path, "a: 1\n", 0640, ""); err != nil {
		t.Fatal(err)
	}
	if info, _ = os.Stat(path); !info.ModTime().Equal(before) {
		t.Error("unchanged content rewrote the file")
	}
	if err := WriteFileIfChanged(path, "", 0640, ""); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(path); len(data) != 0 {
		t.Errorf("content = %q, want empty", data)
	}
	entries, _ := os.ReadDir(filepath.Dir(path))
	if len(entries) != 1 {
		t.Errorf("temp files left behind: %v", entries)
	}
}
