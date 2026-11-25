// Copyright (c) YugabyteDB, Inc.

package server

import (
	"context"
	"fmt"
	"node-agent/app/task"
	"node-agent/app/task/module"
	"node-agent/backoff"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"sync/atomic"
	"syscall"
	"time"
)

const (
	nodeAgentServiceName       = "yb-node-agent.service"
	duplicateUnitCheckInterval = 5 * time.Minute
	// Bounds a check, retries included, as systemctl can hang when dbus is unstable (PLAT-18450).
	duplicateUnitCheckTimeout = time.Minute
)

var (
	// Retries transient systemctl errors. SystemdBackOff waits too long for an upgrade RPC.
	duplicateUnitCheckBackOff backoff.BackOff = backoff.NewSimpleBackOff(2*time.Second, 3)
	// Last result of the duplicate unit check, reported in Ping.
	duplicateServerUnitFound atomic.Bool
)

// HandleUpgradeState is called by the platform after all the required files are copied.
func HandleUpgradeState(
	ctx context.Context,
	config *util.Config,
	upgradeInfo *pb.UpgradeInfo,
) error {
	util.FileLogger().Info(ctx, "Starting the node agent upgrade process")
	// Refuse before touching the config: a second server would write its stale config back over
	// the upgraded one and serve the old cert (PLAT-24106). The error goes back to the YBA caller.
	if err := checkDuplicateServerUnits(ctx); err != nil {
		util.FileLogger().Errorf(ctx, "Cannot upgrade node agent - %s", err.Error())
		return err
	}
	// Delete the certs from past failures.
	err := cleanUpConfigAfterIncompleteUpdate(ctx, config, upgradeInfo)
	if err != nil {
		util.FileLogger().Errorf(ctx,
			"Error in deleting certs - %s from past failures",
			config.String(util.PlatformCertsUpgradeKey),
		)
	}
	// Save the location of new certs in the config.
	config.Update(util.PlatformCertsUpgradeKey, upgradeInfo.CertDir)

	if len(upgradeInfo.PackagePath) > 0 {
		// Run the update script to point to the new version.
		if err := task.HandleUpgradeScript(ctx, config); err != nil {
			util.FileLogger().Errorf(ctx,
				"Error in upgrading to version - %s",
				err.Error(),
			)
			return err
		}
	}
	util.FileLogger().Info(ctx, "Node agent has been succesfully upgraded")
	return nil
}

// findDuplicateServerUnit returns the path of a system-level unit that runs a second server next to
// this user-level one, or "" if there is none.
func findDuplicateServerUnit(ctx context.Context) (string, error) {
	ctx, cancel := context.WithTimeout(ctx, duplicateUnitCheckTimeout)
	defer cancel()
	var path string
	err := backoff.Do(ctx, duplicateUnitCheckBackOff, func(int) error {
		var err error
		path, err = module.DuplicateSystemdUnit(ctx, nodeAgentServiceName)
		return err
	})
	return path, err
}

// checkDuplicateServerUnits returns an error if a duplicate unit is found. If the check itself
// fails (e.g. no systemd), it returns nil so that not knowing never blocks an upgrade.
func checkDuplicateServerUnits(ctx context.Context) error {
	path, err := findDuplicateServerUnit(ctx)
	if err != nil {
		util.FileLogger().Warnf(ctx, "Unable to check for duplicate node agent units - %s", err)
		return nil
	}
	if path != "" {
		return fmt.Errorf("system-level unit %s runs a second node agent next to the user-level "+
			"%s; disable and remove it", path, nodeAgentServiceName)
	}
	return nil
}

// refreshDuplicateServerUnit updates the result reported in Ping. It keeps the last result if the
// check fails, so that a transient error does not hide a duplicate unit.
func refreshDuplicateServerUnit(ctx context.Context) {
	path, err := findDuplicateServerUnit(ctx)
	if err != nil {
		util.FileLogger().Warnf(ctx, "Unable to check for duplicate node agent units - %s", err)
		return
	}
	if path != "" {
		util.FileLogger().Errorf(ctx,
			"System-level unit %s runs a second node agent next to the user-level %s. "+
				"Upgrades are blocked until it is disabled and removed.", path, nodeAgentServiceName)
	}
	duplicateServerUnitFound.Store(path != "")
}

// monitorDuplicateServerUnits checks once before the server starts serving, so that the first Ping
// already reports a duplicate unit, then again every interval until ctx is done. Ping, which YBA
// calls on every poll with a short timeout, only reads the result.
func monitorDuplicateServerUnits(ctx context.Context) {
	refreshDuplicateServerUnit(ctx)
	go func() {
		ticker := time.NewTicker(duplicateUnitCheckInterval)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				refreshDuplicateServerUnit(ctx)
			case <-ctx.Done():
				return
			}
		}
	}()
}

// HandleUpgradedState is called when platform has already rotated the cert and the server key.
// Node agent must restart to use the new cert and the server key.
func HandleUpgradedState(ctx context.Context, config *util.Config) error {
	util.FileLogger().Info(ctx, "Finalizing the upgrade process.")
	// Mark restart pending for the server.
	config.Update(util.NodeAgentRestartKey, true)
	// Stop the service after cleaning up the config.
	pid := os.Getpid()
	defer syscall.Kill(pid, syscall.SIGTERM)
	return nil
}

// HandleRestart is called on process startup before the RPC server is run.
// This fixes the config file after a successful or failed or incomplete update.
func HandleRestart(ctx context.Context, config *util.Config) error {
	util.FileLogger().Info(ctx, "Checking the node-agent state before starting the server.")
	restart := config.Bool(util.NodeAgentRestartKey)
	if restart {
		util.FileLogger().Infof(ctx, "Node Agent was upgraded, thus performing cleanup")
		err := cleanUpConfigAfterUpdate(ctx, config)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Error in cleaning up config after restart - %s", err)
			return err
		}
		updatedVersion := config.String(util.PlatformVersionUpdateKey)
		if updatedVersion != "" {
			config.Update(util.PlatformVersionKey, updatedVersion)
			config.Remove(util.PlatformVersionUpdateKey)
		}
		// Remove previous releases if any and report error to retry.
		if err := removeReleasesExceptCurrent(ctx); err != nil {
			util.FileLogger().
				Errorf(ctx, "Error in cleaning up the releases directory - %s", err.Error())
			return err
		}
	} else {
		// Continue to restart as the state in the platform is not known until it polls.
		util.FileLogger().
			Infof(ctx, "Node Agent was not upgraded, thus continuing the restart")
	}
	return config.Remove(util.NodeAgentRestartKey)
}

// Removes all the releases except the current one and removes version_update from the config.
func removeReleasesExceptCurrent(ctx context.Context) error {
	version := util.CurrentConfig().String(util.PlatformVersionKey)
	err := util.DeleteReleasesExcept(ctx, version)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Unable to read release names to delete previous releases - %s", err)
	}
	return err
}

func cleanUpConfigAfterUpdate(ctx context.Context, config *util.Config) error {
	certDir := config.String(util.PlatformCertsKey)
	upgradeCertDir := config.String(util.PlatformCertsUpgradeKey)
	if upgradeCertDir != "" && upgradeCertDir != certDir {
		util.FileLogger().Infof(ctx, "Starting config clean up after update")
		err := util.DeleteCertsExcept(ctx, []string{upgradeCertDir})
		if err != nil {
			util.FileLogger().Errorf(
				ctx,
				"Error in deleting the certs during cleanup - %s",
				err.Error())
			return err
		}
		// Point current certs to the new certs.
		config.Update(util.PlatformCertsKey, config.String(util.PlatformCertsUpgradeKey))
		config.Remove(util.PlatformCertsUpgradeKey)
	}
	return nil
}

func cleanUpConfigAfterIncompleteUpdate(
	ctx context.Context, config *util.Config, upgradeInfo *pb.UpgradeInfo) error {
	certDir := config.String(util.PlatformCertsKey)
	upgradeCertDir := config.String(util.PlatformCertsUpgradeKey)
	if upgradeCertDir != "" && upgradeCertDir != certDir {
		util.FileLogger().Infof(ctx, "Starting config clean up after incomplete update")
		err := util.DeleteCertsExcept(ctx, []string{certDir, upgradeInfo.CertDir})
		if err != nil {
			util.FileLogger().Errorf(
				ctx,
				"Error in deleting the certs during cleanup - %s",
				err.Error(),
			)
			return err
		}
		config.Remove(util.PlatformCertsUpgradeKey)
	}
	config.Remove(util.PlatformVersionUpdateKey)
	return nil
}
