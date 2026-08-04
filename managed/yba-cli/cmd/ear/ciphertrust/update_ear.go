/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ciphertrust

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateCipherTrustEARCmd represents the ear command
var updateCipherTrustEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere CipherTrust encryption at rest (EAR) configuration",
	Long:    "Update a CipherTrust encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Example: `yba ear ciphertrust update --name <config-name> \
    --auth-type <PASSWORD|REFRESH_TOKEN> [--username <username> --password <password> | --refresh-token <token>]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configNameFlag := util.MustGetFlagString(cmd, "name")
		if util.IsEmptyString(configNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No encryption at rest config name found to update\n",
					formatter.RedColor,
				),
			)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		runUpdateCipherTrustEAR(cmd)
	},
}

func init() {
	updateCipherTrustEARCmd.Flags().SortFlags = false
	updateCipherTrustEARCmd.Flags().
		String("auth-type", "", "[Required] Update CipherTrust auth type. Allowed values: PASSWORD, REFRESH_TOKEN")
	updateCipherTrustEARCmd.MarkFlagRequired("auth-type")
	updateCipherTrustEARCmd.Flags().
		String("username", "", "[Optional] Update CipherTrust username (for auth-type PASSWORD)")
	updateCipherTrustEARCmd.Flags().
		String("password", "", "[Optional] Update CipherTrust password (for auth-type PASSWORD)")
	updateCipherTrustEARCmd.MarkFlagsRequiredTogether("username", "password")
	updateCipherTrustEARCmd.Flags().
		String("refresh-token", "", "[Optional] Update CipherTrust refresh token (for auth-type REFRESH_TOKEN)")
}
