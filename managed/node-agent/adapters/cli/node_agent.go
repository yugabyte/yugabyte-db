// Copyright (c) YugaByte, Inc.
package cli

import (
	"os"

	"node-agent/adapters/cli/node"
	"node-agent/adapters/cli/server"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	rootCmd = &cobra.Command{
		Use:           "node-agent ...",
		Short:         "Command for node agent",
		SilenceUsage:  true,
		SilenceErrors: true,
	}

	versionCmd = &cobra.Command{
		Use:   "version",
		Short: "Get Current Version",
		RunE:  versionCmdHandler,
	}
)

// Execute is the entry for the command.
func Execute() {
	node.SetupNodeCommand(rootCmd)
	server.SetupServerCommand(rootCmd)
	rootCmd.AddCommand(versionCmd)
	if err := rootCmd.Execute(); err != nil {
		util.CliLogger.Errorf(err.Error())
		os.Exit(1)
	}
}

func versionCmdHandler(cmd *cobra.Command, args []string) error {
	config := util.GetConfig()
	util.CliLogger.Infof(config.GetString(util.PlatformVersion))
	return nil
}
