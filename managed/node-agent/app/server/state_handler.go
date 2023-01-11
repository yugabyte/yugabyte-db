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
	util.FileLogger().Info("Starting the node agent upgrade process")
	// Delete the certs from past failures.
	err := cleanUpConfigAfterIncompleteUpdate(ctx, config)
	if err != nil {
		util.FileLogger().Errorf(
			"Error in deleting certs - %s from past failures",
			config.String(util.PlatformCertsUpgradeKey),
		)
	}
	// Save the location of new certs in the config.
	config.Update(util.PlatformCertsUpgradeKey, upgradeInfo.CertDir)

	// Run the update script to point to the new version.
	if err := task.HandleUpgradeScript(ctx, config); err != nil {
		util.FileLogger().Errorf(
			"Error in upgrading to version - %s",
			err.Error(),
		)
		return err
	}
	util.FileLogger().Info("Node agent has been succesfully upgraded")
	return nil
}

// HandleUpgradedState is called when platform has already rotated the cert and the server key.
// Node agent must restart to use the new cert and the server key.
func HandleUpgradedState(ctx context.Context, config *util.Config) error {
	util.FileLogger().Info("Finalizing the upgrade process.")
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
	util.FileLogger().Info("Checking the node-agent state before starting the server.")
	restart := config.Bool(util.NodeAgentRestartKey)
	if restart {
		util.FileLogger().Infof("Node Agent was upgraded, thus performing cleanup")
		err := cleanUpConfigAfterUpdate(ctx, config)
		if err != nil {
			util.FileLogger().Errorf("Error in cleaning up config after restart - %s", err)
			return err
		}
		updatedVersion := config.String(util.PlatformVersionUpdateKey)
		if updatedVersion != "" {
			config.Update(util.PlatformVersionKey, updatedVersion)
			config.Remove(util.PlatformVersionUpdateKey)
		}
	} else {
		util.FileLogger().
			Infof("Node Agent was not upgraded, thus continuing the restart")
		err := cleanUpConfigAfterIncompleteUpdate(ctx, config)
		if err != nil {
			util.FileLogger().Errorf("Error in cleaning up config after restart - %s", err)
			return err
		}
	}
	// Remove previous releases and report error to retry.
	if err := removeReleasesExceptCurrent(); err != nil {
		util.FileLogger().Errorf("Error in cleaning up the releases directory - %s", err.Error())
		return err
	}
	return config.Remove(util.NodeAgentRestartKey)
}

// Removes all the releases except the current one and removes version_update from the config.
func removeReleasesExceptCurrent() error {
	d, err := os.Open(util.ReleaseDir())
	if err != nil {
		util.FileLogger().
			Errorf("Unable to open releases dir to delete previous releases - %s", err)
		return err
	}
	defer d.Close()
	names, err := d.Readdirnames(-1)
	if err != nil {
		util.FileLogger().
			Errorf("Unable to read release names to delete previous releases - %s", err)
		return err
	}
	for _, name := range names {
		if name != util.CurrentConfig().String(util.PlatformVersionKey) {
			err := util.DeleteRelease(name)
			if err != nil {
				return err
			}
		}
	}
	return nil
}

func cleanUpConfigAfterUpdate(ctx context.Context, config *util.Config) error {
	certDir := config.String(util.PlatformCertsKey)
	upgradeCertDir := config.String(util.PlatformCertsUpgradeKey)
	if upgradeCertDir != "" && upgradeCertDir != certDir {
		util.FileLogger().Infof("Starting config clean up after update")
		if err := util.DeleteCerts(certDir); err != nil &&
			!os.IsNotExist(err) {
			util.FileLogger().Errorf(
				"Error in deleting the certs during cleanup - %s",
				err.Error(),
			)
			return err
		}
		// Point current certs to the new certs.
		config.Update(util.PlatformCertsKey, config.String(util.PlatformCertsUpgradeKey))
		config.Remove(util.PlatformCertsUpgradeKey)
	}
	return nil
}

func cleanUpConfigAfterIncompleteUpdate(ctx context.Context, config *util.Config) error {
	certDir := config.String(util.PlatformCertsKey)
	upgradeCertDir := config.String(util.PlatformCertsUpgradeKey)
	if upgradeCertDir != "" && upgradeCertDir != certDir {
		util.FileLogger().Infof("Starting config clean up after incomplete update")
		if err := util.DeleteCerts(upgradeCertDir); err != nil &&
			!os.IsNotExist(err) {
			util.FileLogger().Errorf(
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
