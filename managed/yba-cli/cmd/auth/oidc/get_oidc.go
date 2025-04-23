/*
 * Copyright (c) YugaByte, Inc.
 */

package oidc

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// getOIDCCmd is used to get OIDC authentication configuration for YBA
var getOIDCCmd = &cobra.Command{
	Use:     "get",
	Aliases: []string{"show", "describe"},
	Short:   "Get OIDC configuration for YBA",
	Long:    "Get OIDC configuration for YBA",
	Example: `yba oidc get`,
	Run: func(cmd *cobra.Command, args []string) {
		showInherited := !util.MustGetFlagBool(cmd, "user-set-only")
		getOIDCConfig(showInherited /*inherited*/)
	},
}

func init() {
	getOIDCCmd.Flags().SortFlags = false
	getOIDCCmd.Flags().Bool("user-set-only", false,
		"[Optional] Only show the attributes that were set by the user explicitly.")
}
