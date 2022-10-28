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
	preflightCheck.PersistentFlags().
		BoolP("add_node", "a", false, "Add node instance to on-prem provider (default: false)")
	parentCmd.AddCommand(preflightCheck)
}

func preFlightCheckHandler(cmd *cobra.Command, args []string) {
	util.ConsoleLogger().Debug("Starting Pre Flight Checks")
	util.ConsoleLogger().Debug("Fetching Config from the Platform")
	isAddNodeInstance, _ := cmd.Flags().GetBool("add_node")
	ctx := server.Context()
	config := util.CurrentConfig()
	// Pass empty API token to use JWT.
	providerHandler := task.NewGetProviderHandler()
	// Get provider from the platform.
	err := executor.GetInstance(ctx).ExecuteTask(ctx, providerHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Failed fetching provider from the platform - %s", err)
	}
	provider := providerHandler.Result()
	instanceTypeHandler := task.NewGetInstanceTypeHandler()
	// Get instance type config from the platform.
	err = executor.GetInstance(ctx).ExecuteTask(ctx, instanceTypeHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Failed fetching instance type from the platform - %s", err)
	}
	instanceTypeData := instanceTypeHandler.Result()
	util.ConsoleLogger().Info("Fetched instance type from the platform")

	accessKeysHandler := task.NewGetAccessKeysHandler()
	// Get access key from the platform.
	err = executor.GetInstance(ctx).ExecuteTask(ctx, accessKeysHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Failed fetching config from the platform - %s", err)
	}
	accessKeyData := accessKeysHandler.Result()
	util.ConsoleLogger().Info("Fetched access key from the platform")

	// Prepare the preflight check input.
	util.ConsoleLogger().Info("Running Pre-flight checks")
	preflightCheckHandler := task.NewPreflightCheckHandler(
		provider,
		instanceTypeData,
		accessKeyData,
	)
	err = executor.GetInstance(ctx).
		ExecuteTask(ctx, preflightCheckHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf("Task execution failed - %s", err.Error())
	}
	preflightChecksData := *preflightCheckHandler.Result()
	validationHandler := task.NewValidateNodeInstanceHandler(preflightChecksData)
	util.ConsoleLogger().Info("Evaluating the preflight checks")
	err = executor.GetInstance(ctx).ExecuteTask(
		ctx,
		validationHandler.Handle,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf("Error in validating preflight checks data - %s", err)
	}
	results := *validationHandler.Result()
	util.FileLogger().Infof("Node Instance validation results: %+v", results)
	if !task.OutputPreflightCheck(results) {
		util.ConsoleLogger().Fatal("Preflight checks failed")
	}

	if isAddNodeInstance {
		nodeInstanceHandler := task.NewPostNodeInstanceHandler(preflightChecksData)
		err = executor.GetInstance(ctx).ExecuteTask(
			ctx,
			nodeInstanceHandler.Handle,
		)
		if err != nil {
			util.ConsoleLogger().Fatalf("Error in posting node instance - %s", err)
		}
		nodeInstances := *nodeInstanceHandler.Result()
		nodeUuid := nodeInstances[config.String(util.NodeIpKey)].NodeUuid
		// Update the config with node UUID.
		config.Update(util.NodeIdKey, nodeUuid)
		util.ConsoleLogger().Infof("Node Instance created with Node UUID - %s", nodeUuid)
	}
}
