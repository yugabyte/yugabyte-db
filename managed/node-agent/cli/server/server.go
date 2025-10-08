// Copyright (c) YugabyteDB, Inc.

package server

import (
	"github.com/spf13/cobra"
)

var (
	serverCmd = &cobra.Command{
		Use:   "server ...",
		Short: "Command for server",
	}
)

func SetupServerCommand(parentCmd *cobra.Command) {
	SetupStartCommand(serverCmd)
	parentCmd.AddCommand(serverCmd)
}
