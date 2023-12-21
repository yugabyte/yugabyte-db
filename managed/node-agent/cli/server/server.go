// Copyright (c) YugaByte, Inc.

package server

import (
	"github.com/spf13/cobra"
)

var (
	serverCmd = &cobra.Command{
		Use:    "server ...",
		Short:  "Command for server",
		Hidden: true,
	}
)

func SetupServerCommand(parentCmd *cobra.Command) {
	SetupStartCommand(serverCmd)
	parentCmd.AddCommand(serverCmd)
}
