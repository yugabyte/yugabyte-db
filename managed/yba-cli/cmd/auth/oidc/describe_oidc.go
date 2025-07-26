/*
 * Copyright (c) YugaByte, Inc.
 */

package oidc

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// describeOIDCCmd is used to get OIDC authentication configuration for YBA
var describeOIDCCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"show", "get"},
	Short:   "Describe OIDC configuration for YBA",
	Long:    "Describe OIDC configuration for YBA",
	Example: `yba oidc describe`,
	Run: func(cmd *cobra.Command, args []string) {
		showInherited := !util.MustGetFlagBool(cmd, "user-set-only")
		getOIDCConfig(showInherited /*inherited*/)
	},
}

func init() {
	describeOIDCCmd.Flags().SortFlags = false
	describeOIDCCmd.Flags().Bool("user-set-only", false,
		"[Optional] Only show the attributes that were set by the user explicitly.")
}
