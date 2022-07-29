// Copyright (c) YugaByte, Inc.

package server

import (
	"node-agent/app/service"

	"github.com/spf13/cobra"
)

var (
	startCmd = &cobra.Command{
		Use:   "start ...",
		Short: "Command for starting node-agent service",
		Run:   startServerHandler,
	}
)

func SetupStartCommand(parentCmd *cobra.Command) {
	parentCmd.AddCommand(startCmd)
}

func startServerHandler(cmd *cobra.Command, args []string) {
	service.Start()
}
