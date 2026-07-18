// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"slices"
	"testing"
)

func setupCleanupTestDir(t *testing.T, baseDir string) (string, map[string]struct{}) {
	createdPaths := make(map[string]struct{})
	testHomeDir := filepath.Join(baseDir, "parent_dir", "test_home_dir")
	err := os.MkdirAll(testHomeDir, 0755)
	if err != nil {
		t.Fatalf("Failed to create test directory: %s", err.Error())
	}
	parentDir := filepath.Join(baseDir, "parent_dir")
	symlinkTarget := filepath.Join(parentDir, "symlink_target")
	err = os.MkdirAll(symlinkTarget, 0755)
	if err != nil {
		t.Fatalf("Failed to create symlink target directory: %s", err.Error())
	}
	createdPaths[symlinkTarget] = struct{}{}
	// Create some test files and directories
	subDirs := []string{
		"dir1",
		"dir2",
		"node-agent",
		".yugabyte",
		".yugabyte/inner_dir",
		".yugabytedb",
	}
	for _, dir := range subDirs {
		subPath := filepath.Join(testHomeDir, dir)
		err = os.Mkdir(subPath, 0755)
		if err != nil {
			t.Fatalf("Failed to create subdirectory %s: %s", dir, err.Error())
		}
		createdPaths[subPath] = struct{}{}
	}
	// Add a symlink inside testHomeDir pointing to symlinkTarget outside testHomeDir.
	symlinkPath := filepath.Join(testHomeDir, "symlink_to_target")
	err = os.Symlink(symlinkTarget, symlinkPath)
	if err != nil {
		t.Fatalf("Failed to create symlink: %s", err.Error())
	}
	createdPaths[symlinkPath] = struct{}{}
	return testHomeDir, createdPaths
}

func TestCleanInstance(t *testing.T) {
	baseDir := t.TempDir()
	testHomeDir, createdPaths := setupCleanupTestDir(t, baseDir)
	param := &pb.DestroyServerInput{
		YbHomeDir: testHomeDir,
	}
	handler := &DestroyServerHandler{
		param:  param,
		logOut: util.NewBuffer(module.MaxBufferCapacity),
	}
	err := handler.cleanInstance(context.TODO())
	if err != nil {
		t.Fatalf("cleanInstance failed: %s", err.Error())
	}
	survivedPaths := []string{
		filepath.Join(testHomeDir, "node-agent"),
		filepath.Join(testHomeDir, ".yugabyte"),
		filepath.Join(testHomeDir, ".yugabyte/inner_dir"),
	}
	for path := range createdPaths {
		_, err := os.Stat(path)
		if err == nil {
			if !slices.Contains(survivedPaths, path) {
				t.Fatalf("Expected path %s to be removed, but it still exists", path)
			}
		} else if os.IsNotExist(err) {
			if slices.Contains(survivedPaths, path) {
				t.Fatalf("Expected path %s to survive, but it was removed", path)
			}
		} else {
			t.Fatalf("Error checking path %s: %s", path, err.Error())
		}
	}
}
