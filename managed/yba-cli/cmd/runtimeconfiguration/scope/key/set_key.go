/*
 * Copyright (c) YugaByte, Inc.
 */

package key

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var setKeyCmd = &cobra.Command{
	Use:     "set",
	Aliases: []string{"put"},
	Short:   "Set a YugabyteDB Anywhere runtime configuration scope key value",
	Long: "Set a runtime configuration scope key value in YugabyteDB Anywhere " +
		"Run \"yba runtime-config key-info list\" to get the list of keys in a scope type.",
	Example: `yba runtime-config scope key set --uuid <scope> --name <key-name> --value <value>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		scopeNameFlag, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No scope uuid found to set\n", formatter.RedColor))
		}

		keyNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(keyNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No key name found to set\n", formatter.RedColor))

		}

		valueFlag, err := cmd.Flags().GetString("value")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(valueFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No value found to set\n", formatter.RedColor))

		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		scopeName := util.MustGetFlagString(cmd, "uuid")
		keyName := util.MustGetFlagString(cmd, "name")
		value := util.MustGetFlagString(cmd, "value")
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		SetKey(authAPI, scopeName, keyName, value, true /*logSucess*/)
	},
}

func init() {
	setKeyCmd.Flags().SortFlags = false
	setKeyCmd.Flags().StringP("uuid", "u", "",
		"[Required] The scope UUID of the key to be set.")
	setKeyCmd.MarkFlagRequired("uuid")

	setKeyCmd.Flags().StringP("name", "n", "",
		"[Required] The key name to be set.")
	setKeyCmd.MarkFlagRequired("name")

	setKeyCmd.Flags().StringP("value", "v", "",
		"[Required] The value to be set.")
	setKeyCmd.MarkFlagRequired("value")
}
