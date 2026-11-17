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
		"node-agent",
		".yugabyte",
		".yugabyte/inner_dir",
		".yugabytedb",
		"bin",
		"master",
		"tserver",
		"controller",
		"releases",
		"yb-software",
		"metrics",
		"otel-collector",
		"yugabyte-client-tls-config",
		"yugabyte-tls-config",
		"node_exporter",
		"dont_delete_me",
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
	symlinkPath := filepath.Join(testHomeDir, "cores")
	err = os.Symlink(symlinkTarget, symlinkPath)
	if err != nil {
		t.Fatalf("Failed to create symlink: %s", err.Error())
	}
	createdPaths[symlinkPath] = struct{}{}
	return testHomeDir, createdPaths
}

func TestCleanInstance(t *testing.T) {
	// Without provisioning cleanup, node_exporter's unit stays on the node, and so does its
	// directory.
	testCleanInstance(t, false, "node-agent", ".yugabyte", ".yugabyte/inner_dir", "node_exporter",
		"dont_delete_me")
}

func TestCleanInstanceProvisioningCleanup(t *testing.T) {
	testCleanInstance(t, true, "node-agent", ".yugabyte", ".yugabyte/inner_dir", "dont_delete_me")
}

func testCleanInstance(t *testing.T, isProvisioningCleanup bool, survivors ...string) {
	baseDir := t.TempDir()
	testHomeDir, createdPaths := setupCleanupTestDir(t, baseDir)
	param := &pb.DestroyServerInput{
		YbHomeDir:             testHomeDir,
		IsProvisioningCleanup: isProvisioningCleanup,
	}
	handler := &DestroyServerHandler{
		param:  param,
		logOut: util.NewBuffer(module.MaxBufferCapacity),
	}
	err := handler.cleanInstance(context.TODO())
	if err != nil {
		t.Fatalf("cleanInstance failed: %s", err.Error())
	}
	survivedPaths := []string{}
	for _, survivor := range survivors {
		survivedPaths = append(survivedPaths, filepath.Join(testHomeDir, survivor))
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

func TestEmptyNodeExporterWebConfig(t *testing.T) {
	ybHomeDir := t.TempDir()
	handler := &DestroyServerHandler{
		param:  &pb.DestroyServerInput{YbHomeDir: ybHomeDir},
		logOut: util.NewBuffer(module.MaxBufferCapacity),
	}
	webConfig := filepath.Join(ybHomeDir, nodeExporterWebConfigSubpath)
	// No node_exporter on the node.
	if emptied, err := handler.emptyNodeExporterWebConfig(context.TODO()); err != nil || emptied {
		t.Fatalf("Expected nothing to empty, got emptied=%t err=%v", emptied, err)
	}
	if err := os.MkdirAll(filepath.Dir(webConfig), 0755); err != nil {
		t.Fatalf("Failed to create node_exporter directory: %s", err.Error())
	}
	tlsConfig := "tls_server_config:\n" +
		"  cert_file: \"/missing/node.crt\"\n" +
		"  key_file: \"/missing/node.key\"\n"
	if err := os.WriteFile(webConfig, []byte(tlsConfig), 0640); err != nil {
		t.Fatalf("Failed to write web.yml: %s", err.Error())
	}
	if emptied, err := handler.emptyNodeExporterWebConfig(context.TODO()); err != nil || !emptied {
		t.Fatalf("Expected web.yml to be emptied, got emptied=%t err=%v", emptied, err)
	}
	info, err := os.Stat(webConfig)
	if err != nil || info.Size() != 0 || info.Mode().Perm() != 0640 {
		t.Fatalf("Expected an empty web.yml with mode 0640, got %v err=%v", info, err)
	}
	// Already plain HTTP: nothing to do, so no restart.
	if emptied, err := handler.emptyNodeExporterWebConfig(context.TODO()); err != nil || emptied {
		t.Fatalf("Expected nothing to empty, got emptied=%t err=%v", emptied, err)
	}
}
