// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"errors"
	"node-agent/model"
	"node-agent/util"
	"os"
	"strings"
	"syscall"
	"time"
)

type stateHandlerTask struct {
	pingStateInterval time.Duration
}

func HandleAgentState(config *util.Config) func(ctx context.Context) (any, error) {
	handler := stateHandlerTask{
		pingStateInterval: time.Duration(config.GetInt(util.NodePingInterval)) * time.Second,
	}
	return handler.Process
}

// Runs the state handler task.
func (handler *stateHandlerTask) Process(ctx context.Context) (any, error) {
	config := util.GetConfig()
	nodeAgentId := config.GetString(util.NodeAgentId)
	if nodeAgentId == "" {
		return nil, errors.New("Node Agent ID must be set")
	}
	stateQueryHandler := HandleGetAgentState()
	result, err := stateQueryHandler(ctx)
	if err != nil {
		util.FileLogger.Errorf("Error in getting node agent state. Error: %s", err)
	}
	ptr := result.(*string)
	util.CliLogger.Infof("Agent State is - %s", *ptr)
	state := model.NodeState(*ptr)
	switch state {
	case model.Registering:
		handler.handleRegisteringState(ctx, config)
	case model.Live:
		handler.handleLiveState(ctx, config)
	case model.Upgrade:
		handler.handleUpgradeState(ctx, config)
	case model.Upgrading:
		handler.handleUpgradingState(ctx, config)
	case model.Upgraded:
		handler.handleUpgradedState(ctx, config)
	default:
		util.FileLogger.Debugf("Unhandled state: %s", state)
	}
	return nil, nil
}

func (handler *stateHandlerTask) handleRegisteringState(ctx context.Context, config *util.Config) {
	// NOOP.
}

func (handler *stateHandlerTask) handleLiveState(ctx context.Context, config *util.Config) {
	//NOOP
}

func (handler *stateHandlerTask) handleUpgradeState(ctx context.Context, config *util.Config) {
	util.FileLogger.Info("Starting the upgrade process")
	//Remove previous downloaded package and remove the update_version.
	if err := removeReleasesExceptCurrent(); err != nil {
		util.FileLogger.Errorf("Error while cleaning up the releases directory - %s", err.Error())
		return
	}

	out, err := HandleDownloadPackageScript(config, ctx)
	if err != nil {
		util.CliLogger.Errorf(
			"Error while trying to the run the download updated version script - %s",
			err.Error(),
		)
		return
	}
	out = strings.TrimSuffix(out, "\n")
	out = strings.TrimPrefix(out, "\n")
	util.CliLogger.Infof("Updating to new version - %s", out)
	util.FileLogger.Infof("Updating to new version - %s", out)
	// Set the update_version in the config
	config.Update(util.PlatformVersionUpdate, out)

	// Set the state to upgrading
	result, err := HandlePutAgentState(model.Upgrading, config.GetString(util.PlatformVersion))(ctx)
	if err != nil {
		data, _ := result.(*string)
		util.FileLogger.Errorf("Error while updating agent state to Upgrading - %s", *data)
	}
	util.FileLogger.Info("Changed the node agent state to UPGRADING")
}

func (handler *stateHandlerTask) handleUpgradingState(ctx context.Context, config *util.Config) {
	util.FileLogger.Info("Starting the node agent Upgrading process")
	result, err := HandlePutAgent()(ctx)
	if err != nil {
		errStr := "Error while posting upgrading state to the platform"
		util.CliLogger.Errorf("%s %s", errStr, err)
		util.FileLogger.Errorf("%s %s", errStr, err)
		return
	}
	// Get the latest version certs
	data, ok := result.(*model.NodeAgent)
	if !ok {
		util.CliLogger.Errorf("Error while inferencing type Node Agent to get upgrade certs")
		util.FileLogger.Errorf("Error while inferencing type Node Agent to get upgrade certs")
		return
	}
	newCert, newKey := data.Config.ServerCert, data.Config.ServerKey
	uuid := util.NewUUID().String()

	if err := util.SaveCerts(config, newCert, newKey, uuid); err != nil {
		util.CliLogger.Errorf(
			"Error while saving new certs during upgrading step - %s",
			err.Error(),
		)
		return
	}

	//Delete the certs from past failures.
	if config.GetString(util.PlatformCertsUpgrade) != "" {
		err := util.DeleteCerts(util.PlatformCertsUpgrade)
		//Log the error while deleting the certs but do not suspend the process.
		if err != nil {
			util.CliLogger.Errorf(
				"Error while deleting certs - %s from past failures",
				config.GetString(util.PlatformCertsUpgrade),
			)
		}
	}
	//Save the location of new certs in the config
	config.Update(util.PlatformCertsUpgrade, uuid)

	//Run the update script to change the symlink to the updated version
	if err := HandleUpgradeScript(config, ctx, config.GetString(util.PlatformVersionUpdate)); err != nil {
		util.CliLogger.Errorf(
			"Error while changing the symlink to the updated version - %s",
			err.Error(),
		)
		return
	}

	// Put Upgraded state along with the update version
	util.CliLogger.Infof(
		"Sending the updated version to the platform - %s",
		config.GetString(util.PlatformVersionUpdate),
	)
	if result, err := HandlePutAgentState(model.Upgraded, config.GetString(util.PlatformVersionUpdate))(ctx); err != nil {
		data, _ := result.(*string)
		util.CliLogger.Errorf("Error while updating agent state to Upgraded - %s", *data)
		return
	}

	cleanUpConfigAfterUpdate(ctx, config)
}

func (handler *stateHandlerTask) handleUpgradedState(ctx context.Context, config *util.Config) {
	// Stop the service after cleaning up the config
	pid := os.Getpid()
	defer syscall.Kill(pid, syscall.SIGTERM)

	// Clean up the configp
	if err := cleanUpConfigAfterUpdate(ctx, config); err != nil {
		return
	}

}

func HandleUpgradedStateAfterRestart(ctx context.Context, config *util.Config) error {
	util.CliLogger.Info("Checking the node-agent state before starting the server.")
	result, err := HandleGetAgentState()(ctx)
	if err != nil {
		util.FileLogger.Errorf("Error in getting node agent state. Error: %s", err)
		return err
	}
	ptr := result.(*string)
	if *ptr != model.Upgraded.Name() {
		util.FileLogger.Infof("Node Agent is not in Upgraded State, thus continuing the restart")
		return nil
	}
	//Try cleaning up the config
	err = cleanUpConfigAfterUpdate(ctx, config)
	if err != nil {
		util.FileLogger.Errorf("Error while restarting the node agent - %s", err)
		return err
	}

	//Remove the Current Version Directory and change version to upgrade version
	config.Update(util.PlatformVersion, config.GetString(util.PlatformVersionUpdate))
	if err := removeReleasesExceptCurrent(); err != nil {
		util.FileLogger.Errorf("Error while cleaning up the releases dir - %s", err.Error())
		return err
	}

	//Send Live status to the Platform
	if result, err := HandlePutAgentState(model.Live, config.GetString(util.PlatformVersion))(ctx); err != nil {
		if data, ok := result.(*string); ok {
			util.CliLogger.Errorf("Error while updating agent state to Live - %s", *data)
			util.FileLogger.Errorf("Error while updating agent state to Live - %s", *data)
		} else {
			util.CliLogger.Errorf("Error while updating agent state to Live - %s", err.Error())
			util.FileLogger.Errorf("Error while updating agent state to Live - %s", err.Error())
		}
		return err
	}
	return nil
}

//Removes all the releases except the current one and removes version_update from the config.
func removeReleasesExceptCurrent() error {
	d, err := os.Open(util.GetReleaseDir())
	if err != nil {
		util.FileLogger.Errorf("Unable to open releases dir to delete previous releases - %s", err)
		return err
	}
	defer d.Close()
	names, err := d.Readdirnames(-1)
	if err != nil {
		util.FileLogger.Errorf("Unable to read release names to delete previous releases - %s", err)
		return err
	}
	for _, name := range names {
		if name != config.GetString(util.PlatformVersion) {
			err := util.DeleteRelease(name)
			if err != nil {
				return err
			}
		}
	}
	config.Remove(util.PlatformVersionUpdate)
	return nil
}

func cleanUpConfigAfterUpdate(ctx context.Context, config *util.Config) error {
	util.FileLogger.Infof("Starting config clean up after the update")
	//Point current certs to the new certs
	if config.GetString(util.PlatformCertsUpgrade) != "" {
		//Remove the current certs
		if err := util.DeleteCerts(config.GetString(util.PlatformCerts)); err != nil &&
			!os.IsNotExist(err) {
			util.CliLogger.Errorf("Error while deleting the certs during cleanup - %s", err.Error())
			util.FileLogger.Errorf(
				"Error while deleting the certs during cleanup - %s",
				err.Error(),
			)
			return err
		}
		config.Update(util.PlatformCerts, config.GetString(util.PlatformCertsUpgrade))
		config.Remove(util.PlatformCertsUpgrade)
	}
	return nil
}
