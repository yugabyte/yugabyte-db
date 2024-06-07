// Copyright (c) YugaByte, Inc.

package server

import (
	svc "node-agent/app/server"

	"github.com/spf13/cobra"
)

var (
	startCmd = &cobra.Command{
		Use: "start ...",
		Short: "Start node-agent server. For testing only. For routine operations, " +
			"use 'systemctl start yb-node-agent.service'.",
		Run: startServerHandler,
	}
)

func SetupStartCommand(parentCmd *cobra.Command) {
	parentCmd.AddCommand(startCmd)
}

func startServerHandler(cmd *cobra.Command, args []string) {
	svc.Start()
}
