/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ciphertrust

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createCipherTrustEARCmd represents the ear command
var createCipherTrustEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere CipherTrust encryption at rest configuration",
	Long:    "Create a CipherTrust encryption at rest configuration in YugabyteDB Anywhere",
	Example: `yba ear ciphertrust create --name <config-name> \
    --manager-url <ciphertrust-manager-url> --auth-type <PASSWORD|REFRESH_TOKEN> \
    [--username <username> --password <password> | --refresh-token <token>] \
    --key-name <key-name> --key-algorithm AES --key-size <128|192|256>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
		authType := util.MustGetFlagString(cmd, "auth-type")
		if strings.EqualFold(authType, "PASSWORD") {
			username := util.MaybeGetFlagString(cmd, "username")
			password := util.MaybeGetFlagString(cmd, "password")
			if len(username) == 0 || len(password) == 0 {
				cmd.Help()
				logrus.Fatalln(
					formatter.Colorize(
						"--username and --password are required for PASSWORD auth-type\n",
						formatter.RedColor,
					),
				)
			}
		} else if strings.EqualFold(authType, "REFRESH_TOKEN") {
			refreshToken := util.MaybeGetFlagString(cmd, "refresh-token")
			if len(refreshToken) == 0 {
				cmd.Help()
				logrus.Fatalln(
					formatter.Colorize(
						"--refresh-token is required for REFRESH_TOKEN auth-type\n",
						formatter.RedColor,
					),
				)
			}
		} else {
			cmd.Help()
			logrus.Fatalln(formatter.Colorize(
				"Invalid --auth-type. Allowed values: PASSWORD, REFRESH_TOKEN\n",
				formatter.RedColor,
			))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		runCreateCipherTrustEAR(cmd)
	},
}

func init() {
	createCipherTrustEARCmd.Flags().SortFlags = false

	createCipherTrustEARCmd.Flags().String("manager-url", "",
		"[Required] CipherTrust Manager URL.")
	createCipherTrustEARCmd.MarkFlagRequired("manager-url")

	createCipherTrustEARCmd.Flags().String("auth-type", "PASSWORD",
		"[Optional]Authentication type. Allowed values (case sensitive): PASSWORD, REFRESH_TOKEN")

	createCipherTrustEARCmd.Flags().
		String("username", "", "[Optional] CipherTrust username (for auth-type PASSWORD)")
	createCipherTrustEARCmd.Flags().
		String("password", "", "[Optional] CipherTrust password (for auth-type PASSWORD)")
	createCipherTrustEARCmd.MarkFlagsRequiredTogether("username", "password")

	createCipherTrustEARCmd.Flags().
		String("refresh-token", "", "[Optional] CipherTrust refresh token (for auth-type REFRESH_TOKEN)")

	createCipherTrustEARCmd.Flags().String("key-name", "", "[Required] CipherTrust key name")
	createCipherTrustEARCmd.MarkFlagRequired("key-name")
	createCipherTrustEARCmd.Flags().
		String("key-algorithm", "AES", "[Optional] CipherTrust key algorithm. Allowed values (case sensitive): AES")
	createCipherTrustEARCmd.Flags().
		Int("key-size", 256, "[Optional] CipherTrust key size for algorithm AES. Allowed values: 128, 192, 256")
}
