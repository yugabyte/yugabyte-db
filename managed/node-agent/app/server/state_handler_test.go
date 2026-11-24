// Copyright (c) YugabyteDB, Inc.

package server

import (
	"context"
	"errors"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func stubSystemUnit(t *testing.T, cgroup string, state module.SystemUnitState, err error) {
	originalShow, originalCgroup := module.ShowSystemUnitState, module.SelfCgroupPath
	t.Cleanup(func() {
		module.ShowSystemUnitState, module.SelfCgroupPath = originalShow, originalCgroup
	})
	module.SelfCgroupPath = filepath.Join(t.TempDir(), "cgroup")
	if err := os.WriteFile(module.SelfCgroupPath, []byte(cgroup+"\n"), 0644); err != nil {
		t.Fatal(err)
	}
	module.ShowSystemUnitState = func(context.Context, string) (module.SystemUnitState, error) {
		return state, err
	}
}

const userUnitCgroup = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/" +
	nodeAgentServiceName

// The upgrade must be refused before the config is touched (PLAT-24106).
func TestHandleUpgradeStateRefusesDuplicateUnit(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, module.SystemUnitState{
		LoadState:     "loaded",
		UnitFileState: "disabled",
		ActiveState:   "activating",
		FragmentPath:  "/etc/systemd/system/" + nodeAgentServiceName,
	}, nil)
	config := util.CurrentConfig()
	before := config.String(util.PlatformCertsUpgradeKey)
	err := HandleUpgradeState(context.Background(), config, &pb.UpgradeInfo{CertDir: "certs/new"})
	if err == nil {
		t.Fatal("expected the upgrade to be refused")
	}
	if got := config.String(util.PlatformCertsUpgradeKey); got != before {
		t.Fatalf("config changed before refusing: %q -> %q", before, got)
	}
}

// Not being able to tell (e.g. no systemd) must not block upgrades.
func TestCheckDuplicateServerUnitsFailsOpen(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, module.SystemUnitState{}, errors.New("no systemd"))
	if err := checkDuplicateServerUnits(context.Background()); err != nil {
		t.Fatalf("expected no error, got %v", err)
	}
}

func TestCachedDuplicateServerUnitRefreshesInBackground(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, module.SystemUnitState{
		LoadState: "loaded", UnitFileState: "enabled", ActiveState: "active",
		FragmentPath: "/etc/systemd/system/" + nodeAgentServiceName,
	}, nil)
	duplicateUnitCache.Lock()
	duplicateUnitCache.checkedAt, duplicateUnitCache.found = time.Time{}, false
	duplicateUnitCache.Unlock()
	t.Cleanup(func() {
		duplicateUnitCache.Lock()
		duplicateUnitCache.checkedAt, duplicateUnitCache.found = time.Time{}, false
		duplicateUnitCache.Unlock()
	})
	ctx := context.Background()
	cachedDuplicateServerUnit(ctx)
	deadline := time.Now().Add(5 * time.Second)
	for !cachedDuplicateServerUnit(ctx) {
		if time.Now().After(deadline) {
			t.Fatal("background refresh did not report the duplicate unit")
		}
		time.Sleep(10 * time.Millisecond)
	}
}
