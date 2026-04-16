/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oidc

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// disableOIDCCmd is used to disable OIDC authentication for YBA
var disableOIDCCmd = &cobra.Command{
	Use:     "disable",
	Aliases: []string{"delete"},
	Short:   "Disable OIDC configuration for YBA",
	Long:    "Disable OIDC configuration  for YBA",
	Example: `yba oidc disable --reset-fields client-id,client-secret`,
	Run: func(cmd *cobra.Command, args []string) {
		resetKeys := util.MaybeGetFlagStringSlice(cmd, "reset-fields")
		runtimeKeysToDelete := map[string]bool{
			util.ToggleOIDCKey:   true,
			util.SecurityTypeKey: true,
		}
		for _, key := range resetKeys {
			if runtimeKey, ok := util.GetOIDCKeyForResetFlag(key); ok {
				runtimeKeysToDelete[runtimeKey] = true
			} else {
				logrus.Warn(formatter.Colorize(
					"Unrecognized key: "+key+". Skipping it.\n", formatter.YellowColor))
			}
		}
		disableOIDC(util.MustGetFlagBool(cmd, "reset-all"), runtimeKeysToDelete)
	},
}

func init() {
	disableOIDCCmd.Flags().SortFlags = false
	disableOIDCCmd.Flags().Bool("reset-all", false,
		"[Optional] Reset all OIDC fields to default values")
	disableOIDCCmd.Flags().StringSlice("reset-fields", []string{},
		"[Optional] Reset specific OIDC fields to default values. "+
			"Comma separated list of fields. Example: --reset-fields <field1>,<field2>\n"+
			formatter.Colorize(
				"           Allowed values: client-id, client-secret, discovery-url, scope, email-attribute, default-role \n"+
					"           refresh-token-endpoint, provider-configuration, auto-create-user, group-claim",
				formatter.GreenColor,
			))

}
