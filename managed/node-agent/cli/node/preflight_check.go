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
		Run:   preflightCheckHandler,
	}
)

func SetupPreflightCheckCommand(parentCmd *cobra.Command) {
	preflightCheck.PersistentFlags().
		BoolP("add_node", "a", false, "Add node instance to on-prem provider (default: false)")
	parentCmd.AddCommand(preflightCheck)
}

func preflightCheckHandler(cmd *cobra.Command, args []string) {
	ctx := server.Context()
	util.ConsoleLogger().Debug(ctx, "Starting Pre Flight Checks")
	util.ConsoleLogger().Debug(ctx, "Fetching Config from the Platform")
	isAddNodeInstance, _ := cmd.Flags().GetBool("add_node")
	config := util.CurrentConfig()
	// Pass empty API token to use JWT.
	providerHandler := task.NewGetProviderHandler()
	// Get provider from the platform.
	err := executor.GetInstance().ExecuteTask(ctx, providerHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Failed fetching provider from the platform - %s", err)
	}
	provider := providerHandler.Result()
	instanceTypeHandler := task.NewGetInstanceTypeHandler()
	// Get instance type config from the platform.
	err = executor.GetInstance().ExecuteTask(ctx, instanceTypeHandler.Handle)
	if err != nil {
		util.ConsoleLogger().
			Fatalf(ctx, "Failed fetching instance type from the platform - %s", err)
	}
	instanceTypeData := instanceTypeHandler.Result()
	util.ConsoleLogger().Info(ctx, "Fetched instance type from the platform")

	accessKeysHandler := task.NewGetAccessKeysHandler()
	// Get access key from the platform.
	err = executor.GetInstance().ExecuteTask(ctx, accessKeysHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Failed fetching config from the platform - %s", err)
	}
	accessKeyData := accessKeysHandler.Result()
	util.ConsoleLogger().Info(ctx, "Fetched access key from the platform")

	// Prepare the preflight check input.
	util.ConsoleLogger().Info(ctx, "Running Pre-flight checks")
	preflightCheckParam := task.CreatePreflightCheckParam(
		provider, instanceTypeData, accessKeyData)
	preflightCheckHandler := task.NewPreflightCheckHandler(preflightCheckParam)
	err = executor.GetInstance().
		ExecuteTask(ctx, preflightCheckHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Task execution failed - %s", err.Error())
	}
	preflightChecksOutput := *preflightCheckHandler.Result()
	validationHandler := task.NewValidateNodeInstanceHandler(preflightChecksOutput)
	util.ConsoleLogger().Info(ctx, "Evaluating the preflight checks")
	err = executor.GetInstance().ExecuteTask(
		ctx,
		validationHandler.Handle,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error in validating preflight checks data - %s", err)
	}
	results := *validationHandler.Result()
	util.FileLogger().Infof(ctx, "Node Instance validation results: %+v", results)
	if !task.OutputPreflightCheck(results) {
		util.ConsoleLogger().Fatal(ctx, "Preflight checks failed")
	}

	if isAddNodeInstance {
		nodeInstanceHandler := task.NewPostNodeInstanceHandler(preflightChecksOutput)
		err = executor.GetInstance().ExecuteTask(
			ctx,
			nodeInstanceHandler.Handle,
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Error in posting node instance - %s", err)
		}
		nodeInstances := *nodeInstanceHandler.Result()
		nodeUuid := nodeInstances[config.String(util.NodeIpKey)].NodeUuid
		// Update the config with node UUID.
		config.Update(util.NodeIdKey, nodeUuid)
		util.ConsoleLogger().Infof(ctx, "Node Instance created with Node UUID - %s", nodeUuid)
	}
}
