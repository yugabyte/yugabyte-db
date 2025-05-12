/*
 * Copyright (c) YugaByte, Inc.
 */

package oidc

import (
	"github.com/spf13/cobra"
)

// OIDCCmd set of commands are used to manage OIDC in YugabyteDB Anywhere
var OIDCCmd = &cobra.Command{
	Use:     "oidc",
	Aliases: []string{"sso"},
	Short:   "Manage YugabyteDB Anywhere OIDC configuration",
	Long:    "Manage YugabyteDB Anywhere OIDC (OpenID Connect) configuration",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	OIDCCmd.Flags().SortFlags = false
	OIDCCmd.AddCommand(configureOIDCCmd)
	OIDCCmd.AddCommand(disableOIDCCmd)
	OIDCCmd.AddCommand(describeOIDCCmd)
}
