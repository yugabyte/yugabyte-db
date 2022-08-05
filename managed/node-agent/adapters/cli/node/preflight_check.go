// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"errors"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	preflightCheck = &cobra.Command{
		Use:   "preflight-check",
		Short: "Check Preflight steps in the node",
		RunE:  preFlightCheckHandler,
	}
)

func SetupPreflightCheckCommand(parentCmd *cobra.Command) {
	parentCmd.AddCommand(preflightCheck)
}

func preFlightCheckHandler(cmd *cobra.Command, args []string) error {
	util.CliLogger.Debug("Starting Pre Flight Checks")
	util.CliLogger.Debug("Fetching Config from the Platform")
	ctx := context.Background()
	//Get Preflight checks config from platform.
	response, err := taskExecutor.ExecuteTask(ctx, task.HandleGetPlatformConfig())
	if err != nil {
		util.CliLogger.Errorf("Failed fetching config from the platform - %s", err)
		return err
	}
	util.CliLogger.Info("Fetched Config from the Platform")
	data, ok := response.(*model.NodeInstanceType)
	if !ok {
		panic("NodeInstanceType type inference error")
	}

	util.CliLogger.Info("Running Pre-flight checks")
	response, err = taskExecutor.ExecuteTask(ctx, task.HandlePreflightCheck(*data))
	if err != nil {
		util.CliLogger.Errorf("Task Execution Failed - %s", err.Error())
		return err
	}
	preflightChecksData, ok := response.(map[string]model.PreflightCheckVal)
	if !ok {
		panic("Type Inference failed for preflight checks")
	}
	task.OutputPreflightCheck(preflightChecksData)

	util.CliLogger.Info("Evaluating the Preflight Checks")
	response, err = taskExecutor.ExecuteTask(
		ctx,
		task.HandleSendNodeCapability(preflightChecksData),
	)
	if err != nil {
		util.CliLogger.Errorf("Preflight Checks Failed - %s", err)
		return err
	}
	nodeCapData, ok := response.(*map[string]model.NodeCapabilityResponse)
	if !ok {
		errStr := "Preflight Checks success response inference error"
		util.CliLogger.Errorf(errStr)
		return errors.New(errStr)
	}
	nodeUuid := (*nodeCapData)[appConfig.GetString(util.NodeIP)].NodeUuid
	//Update the config with NodeUUID
	appConfig.Update(util.NodeId, nodeUuid)
	util.CliLogger.Infof("Node Instance created with Node UUID - %s", nodeUuid)
	return nil
}
