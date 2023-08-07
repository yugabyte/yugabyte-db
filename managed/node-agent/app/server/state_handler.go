// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"node-agent/app/task"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"syscall"
)

// HandleUpgradeState is called by the platform after all the required files are copied.
func HandleUpgradeState(
	ctx context.Context,
	config *util.Config,
	upgradeInfo *pb.UpgradeInfo,
) error {
	util.FileLogger().Info(ctx, "Starting the node agent upgrade process")
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

	// Run the update script to point to the new version.
	if err := task.HandleUpgradeScript(ctx, config); err != nil {
		util.FileLogger().Errorf(ctx,
			"Error in upgrading to version - %s",
			err.Error(),
		)
		return err
	}
	util.FileLogger().Info(ctx, "Node agent has been succesfully upgraded")
	return nil
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
