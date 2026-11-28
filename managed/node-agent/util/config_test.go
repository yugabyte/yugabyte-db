/*
 * Copyright (c) YugabyteDB, Inc.
 */
package util

import (
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

func TestConfig(t *testing.T) {
	config := CurrentConfig()
	want, got := "nodeName", config.String(NodeNameKey)
	if want != got {
		t.Fatalf("Expected %s but got %s", want, got)
	}
	config.Update("test1.test2", "value2")
	config.Remove("test1.test2")
	want, got = "", config.String("test1.test2")
	if want != got {
		t.Fatalf("Expected %s but got %s", want, got)
	}
}

// A server that loaded the config before an upgrade rewrote it must see the upgrade after Reload,
// and its later writes must not put the old values back (PLAT-24106).
func TestConfigReloadPicksUpExternalWrites(t *testing.T) {
	configPath := filepath.Join(t.TempDir(), "reload-config")
	stale, err := ConfigWithName(configPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := stale.Update(PlatformCertsKey, "certs/old"); err != nil {
		t.Fatal(err)
	}
	// Another process upgrades the node agent and rewrites the file.
	upgraded := []byte("platform:\n  certs: certs/new\nnode:\n  restart: true\n")
	if err := os.WriteFile(configPath+".yml", upgraded, 0644); err != nil {
		t.Fatal(err)
	}
	if err := stale.Reload(); err != nil {
		t.Fatal(err)
	}
	if got := stale.Bool(NodeAgentRestartKey); !got {
		t.Fatalf("expected restart flag after reload")
	}
	if err := stale.Remove(NodeAgentRestartKey); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(configPath + ".yml")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), "certs/new") || strings.Contains(string(data), "certs/old") {
		t.Fatalf("stale config written back:\n%s", data)
	}
}

// Two processes that loaded the config at the same time must not undo each other's writes.
func TestConfigWritesKeepOtherProcessWrites(t *testing.T) {
	configPath := filepath.Join(t.TempDir(), "shared-config")
	server, err := ConfigWithName(configPath)
	if err != nil {
		t.Fatal(err)
	}
	// A second process: its own Config on the same file, loaded before the server writes.
	other := &Config{rwLock: &sync.RWMutex{}}
	if other.viperInstance, err = server.readFile(); err != nil {
		t.Fatal(err)
	}
	if err := server.Update(PlatformCertsUpgradeKey, "certs/new"); err != nil {
		t.Fatal(err)
	}
	if err := other.Update(NodeAgentLogLevelKey, "2"); err != nil {
		t.Fatal(err)
	}
	updated, err := other.CompareAndUpdate(PlatformCertsUpgradeKey, nil, "certs/stale")
	if err != nil || updated {
		t.Fatalf("CompareAndUpdate compared against a stale snapshot: %v %v", updated, err)
	}
	if err := server.Reload(); err != nil {
		t.Fatal(err)
	}
	if got := server.String(PlatformCertsUpgradeKey); got != "certs/new" {
		t.Fatalf("server's write was undone, got %q", got)
	}
	if got := server.String(NodeAgentLogLevelKey); got != "2" {
		t.Fatalf("other process's write was lost, got %q", got)
	}
}
