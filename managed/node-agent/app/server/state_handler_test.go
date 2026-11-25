// Copyright (c) YugabyteDB, Inc.

package server

import (
	"context"
	"errors"
	"node-agent/app/task/module"
	"node-agent/backoff"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"testing"
)

// stubSystemUnit makes systemctl return the given results in order, repeating the last one, and
// returns the number of calls.
func stubSystemUnit(t *testing.T, cgroup string, results ...systemUnitResult) *int {
	originalShow, originalCgroup := module.ShowSystemUnitState, module.SelfCgroupPath
	originalBackOff := duplicateUnitCheckBackOff
	t.Cleanup(func() {
		module.ShowSystemUnitState, module.SelfCgroupPath = originalShow, originalCgroup
		duplicateUnitCheckBackOff = originalBackOff
		duplicateServerUnitFound.Store(false)
	})
	duplicateUnitCheckBackOff = backoff.NewSimpleBackOff(0, 3)
	module.SelfCgroupPath = filepath.Join(t.TempDir(), "cgroup")
	if err := os.WriteFile(module.SelfCgroupPath, []byte(cgroup+"\n"), 0644); err != nil {
		t.Fatal(err)
	}
	calls := new(int)
	module.ShowSystemUnitState = func(context.Context, string) (module.SystemUnitState, error) {
		result := results[min(*calls, len(results)-1)]
		*calls++
		return result.state, result.err
	}
	return calls
}

type systemUnitResult struct {
	state module.SystemUnitState
	err   error
}

var (
	runningSystemUnit = systemUnitResult{state: module.SystemUnitState{
		LoadState:     "loaded",
		UnitFileState: "enabled",
		ActiveState:   "active",
		FragmentPath:  "/etc/systemd/system/" + nodeAgentServiceName,
	}}
	noSystemUnit     = systemUnitResult{state: module.SystemUnitState{LoadState: "not-found"}}
	systemctlFailure = systemUnitResult{err: errors.New("dbus connection failed")}
)

const userUnitCgroup = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/" +
	nodeAgentServiceName

// The upgrade must be refused before the config is touched (PLAT-24106).
func TestHandleUpgradeStateRefusesDuplicateUnit(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, runningSystemUnit)
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

// A transient systemctl error must not let the upgrade through (PLAT-18450).
func TestCheckDuplicateServerUnitsRetries(t *testing.T) {
	calls := stubSystemUnit(t, userUnitCgroup, systemctlFailure, runningSystemUnit)
	if err := checkDuplicateServerUnits(context.Background()); err == nil {
		t.Fatal("expected the duplicate unit to be found after a retry")
	}
	if *calls != 2 {
		t.Fatalf("expected 2 systemctl calls, got %d", *calls)
	}
}

// Not being able to tell (e.g. no systemd) must not block upgrades.
func TestCheckDuplicateServerUnitsFailsOpen(t *testing.T) {
	calls := stubSystemUnit(t, userUnitCgroup, systemctlFailure)
	if err := checkDuplicateServerUnits(context.Background()); err != nil {
		t.Fatalf("expected no error, got %v", err)
	}
	if *calls != 3 {
		t.Fatalf("expected 3 systemctl calls, got %d", *calls)
	}
}

// Ping reports the last result, which a failed check must not reset.
func TestRefreshDuplicateServerUnitKeepsLastResultOnError(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, runningSystemUnit, systemctlFailure)
	ctx := context.Background()
	refreshDuplicateServerUnit(ctx)
	if !duplicateServerUnitFound.Load() {
		t.Fatal("expected the duplicate unit to be reported")
	}
	refreshDuplicateServerUnit(ctx)
	if !duplicateServerUnitFound.Load() {
		t.Fatal("a failed check reset the result")
	}
	stubSystemUnit(t, userUnitCgroup, noSystemUnit)
	refreshDuplicateServerUnit(ctx)
	if duplicateServerUnitFound.Load() {
		t.Fatal("expected no duplicate unit after it was removed")
	}
}

// The first check runs before the server serves, so the first Ping already reports the result.
func TestMonitorDuplicateServerUnitsChecksBeforeReturning(t *testing.T) {
	stubSystemUnit(t, userUnitCgroup, runningSystemUnit)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	monitorDuplicateServerUnits(ctx)
	if !duplicateServerUnitFound.Load() {
		t.Fatal("expected the duplicate unit to be reported")
	}
}
