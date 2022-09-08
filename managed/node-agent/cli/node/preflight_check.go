// Copyright (c) YugaByte, Inc.

package node

import (
	"node-agent/app/executor"
	"node-agent/app/server"
	"node-agent/app/task"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	preflightCheck = &cobra.Command{
		Use:   "preflight-check",
		Short: "Check Preflight steps in the node",
		Run:   preFlightCheckHandler,
	}
)

func SetupPreflightCheckCommand(parentCmd *cobra.Command) {
	parentCmd.AddCommand(preflightCheck)
}

func preFlightCheckHandler(cmd *cobra.Command, args []string) {
	util.ConsoleLogger().Debug("Starting Pre Flight Checks")
	util.ConsoleLogger().Debug("Fetching Config from the Platform")
	ctx := server.Context()
	config := util.CurrentConfig()
	platformConfigHandler := task.NewGetPlatformCurrentConfigHandler()
	// Get Preflight checks config from platform.
	err := executor.GetInstance(ctx).ExecuteTask(ctx, platformConfigHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Failed fetching config from the platform - %s", err)
	}
	data := platformConfigHandler.Result()
	util.ConsoleLogger().Info("Fetched Config from the Platform")

	util.ConsoleLogger().Info("Running Pre-flight checks")
	preflightCheckHandler := task.NewPreflightCheckHandler(*data)
	err = executor.GetInstance(ctx).
		ExecuteTask(ctx, preflightCheckHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Task Execution Failed - %s", err.Error())
	}
	preflightChecksData := *preflightCheckHandler.Result()
	task.OutputPreflightCheck(preflightChecksData)

	util.ConsoleLogger().Info("Evaluating the Preflight Checks")
	nodeCapabilityHandler := task.NewSendNodeCapabilityHandler(preflightChecksData)
	err = executor.GetInstance(ctx).ExecuteTask(
		ctx,
		nodeCapabilityHandler.Handle,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf("Preflight Checks Failed - %s", err)
	}
	nodeCapData := *nodeCapabilityHandler.Result()
	nodeUuid := nodeCapData[config.String(util.NodeIpKey)].NodeUuid
	// Update the config with node UUID.
	config.Update(util.NodeIdKey, nodeUuid)
	util.ConsoleLogger().Infof("Node Instance created with Node UUID - %s", nodeUuid)
}
