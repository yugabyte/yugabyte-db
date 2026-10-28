// Copyright (c) YugabyteDB, Inc.

package cli

import (
	"os"

	srv "node-agent/app/server"
	"node-agent/cli/federation"
	"node-agent/cli/node"
	"node-agent/cli/server"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	rootCmd = &cobra.Command{
		Use:           "node-agent ...",
		Short:         "Command for node agent",
		SilenceUsage:  true,
		SilenceErrors: true,
		// Before the command runs: the FIPS mode is fixed for the life of the process.
		PersistentPreRun: applyFipsMode,
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
	federation.SetupFederationCommand(rootCmd)
	rootCmd.AddCommand(versionCmd)
	if err := rootCmd.Execute(); err != nil {
		util.ConsoleLogger().Error(srv.Context(), err.Error())
		os.Exit(1)
	}
}

func versionCmdHandler(cmd *cobra.Command, args []string) error {
	config := util.CurrentConfig()
	util.ConsoleLogger().Info(srv.Context(), config.String(util.PlatformVersionKey))
	return nil
}

func applyFipsMode(cmd *cobra.Command, args []string) {
	util.ApplyFipsMode(util.NodeAgentFipsEnabled(cmd.Flags()))
}
