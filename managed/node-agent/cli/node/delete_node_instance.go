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
	deleteNodeInstance = &cobra.Command{
		Use:   "delete-instance",
		Short: "Delete node instance from provider without unregistering",
		Run:   deleteNodeInstanceHandler,
	}
)

func SetupDeleteNodeInstanceCmd(parentCmd *cobra.Command) {
	parentCmd.AddCommand(deleteNodeInstance)
}

func deleteNodeInstanceHandler(cmd *cobra.Command, args []string) {
	ctx := server.Context()
	config := util.CurrentConfig()
	// Pass empty API token to use JWT.
	deleteHandler := task.NewDeleteNodeInstanceHandler()
	nodeIp := config.String(util.NodeIpKey)
	providerId := config.String(util.ProviderIdKey)
	util.ConsoleLogger().Infof(ctx,
		"Deleting Node Instance with IP %s from provider %s", nodeIp, providerId)
	// Get provider from the platform.
	err := executor.GetInstance().ExecuteTask(ctx, deleteHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx,
			"Failed to delete the node instance with IP %s from provider %s - %s",
			nodeIp, providerId, err)
	}
	util.ConsoleLogger().Infof(ctx,
		"Node Instance with IP %s deleted successfully from provider %s", nodeIp, providerId)
}
