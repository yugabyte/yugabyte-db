/*
 * Copyright (c) YugabyteDB, Inc.
 */
package util

import (
	"os"
	"path/filepath"
	"testing"
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
