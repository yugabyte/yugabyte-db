// Copyright (c) YugaByte, Inc.

package server

import (
	svc "node-agent/app/server"

	"github.com/spf13/cobra"
)

var (
	startCmd = &cobra.Command{
		Use:   "start ...",
		Short: "Command for starting node-agent server",
		Run:   startServerHandler,
	}
)

func SetupStartCommand(parentCmd *cobra.Command) {
	parentCmd.AddCommand(startCmd)
}

func startServerHandler(cmd *cobra.Command, args []string) {
	svc.Start()
}
